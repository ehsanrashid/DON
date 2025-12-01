/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "tbbase.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #if !defined(NOMINMAX)
        #define NOMINMAX  // Disable min()/max() macros
    #endif
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <sdkddkver.h>
    #if defined(_WIN32_WINNT) && _WIN32_WINNT < _WIN32_WINNT_WIN7
        #undef _WIN32_WINNT
    #endif
    #if !defined(_WIN32_WINNT)
        // Force to include needed API prototypes
        #define _WIN32_WINNT _WIN32_WINNT_WIN7  // or _WIN32_WINNT_WIN10
    #endif
    #undef UNICODE
    #include <windows.h>
    #if defined(small)
        #undef small
    #endif
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
#endif

#include "../bitboard.h"
#include "../misc.h"
#include "../movegen.h"
#include "../position.h"
#include "../search.h"
#include "../types.h"
#include "../uci.h"
#include "../ucioption.h"

using namespace DON::Tablebases;

namespace DON {

namespace {

enum Endian : std::uint8_t {
    Big,
    Little
};
// Used as template parameter
enum TBType : std::uint8_t {
    WDL,
    DTZ
};
// Each table has a set of flags: all of them refer to DTZ-tables, the last one to WDL-tables
enum TBFlag : std::uint8_t {
    AC          = 1,
    Mapped      = 2,
    WinPlies    = 4,
    LossPlies   = 8,
    Wide        = 16,
    SingleValue = 128
};

// Max number of supported piece
constexpr std::uint32_t TBPieces = 7;
// Max DTZ supported (2 times), large enough to deal with the syzygy TB limit.
constexpr int MAX_DTZ = 1 << 18;

constexpr std::string_view WDLExt = ".rtbw";
constexpr std::string_view DTZExt = ".rtbz";

// clang-format off
constexpr StdArray<int, 5>          WDLMap{1, 3, 0, 2, 0};
constexpr StdArray<std::int32_t, 5> WDLToRank{-MAX_DTZ, -MAX_DTZ + 101, 0, +MAX_DTZ - 101, +MAX_DTZ};
constexpr StdArray<Value, 5>        WDLToValue{VALUE_MATED_IN_MAX_PLY + 1, VALUE_DRAW - 2, VALUE_DRAW, VALUE_DRAW + 2, VALUE_MATES_IN_MAX_PLY - 1};

StdArray<std::size_t, SQUARE_NB>     PawnsMap;
StdArray<std::size_t, SQUARE_NB>     B1H1H7Map;
StdArray<std::size_t, SQUARE_NB>     A1D1D4Map;
StdArray<std::size_t, 10, SQUARE_NB> KKMap;  // [A1D1D4Map][SQUARE_NB]

StdArray<std::size_t, 6, SQUARE_NB>   Binomial;     // [k][n] k elements from a set of n elements
StdArray<std::size_t, 6, SQUARE_NB>   LeadPawnIdx;  // [leadPawnCnt][SQUARE_NB]
StdArray<std::size_t, 6, FILE_NB / 2> LeadPawnSize; // [leadPawnCnt][FILE_A..FILE_D]
// clang-format on

constexpr int off_A1H8(Square s) noexcept { return int(rank_of(s)) - int(file_of(s)); }

// Comparison function to sort leading pawns in ascending PawnsMap[] order
bool pawns_comp(Square s1, Square s2) noexcept { return PawnsMap[s1] < PawnsMap[s2]; }

template<typename T, int Half = sizeof(T) / 2, int End = sizeof(T) - 1>
void swap_endian(T& x) noexcept {
    static_assert(std::is_unsigned_v<T>, "Argument of swap_endian not unsigned");

    std::uint8_t tmp, *c = (std::uint8_t*) (&x);
    for (int i = 0; i < Half; ++i)
        tmp = c[i], c[i] = c[End - i], c[End - i] = tmp;
}
template<>
void swap_endian<std::uint8_t>(std::uint8_t&) noexcept {}

template<typename T, Endian Endian>
T number(void* addr) noexcept {
    T v;

    if (std::uintptr_t(addr) & (alignof(T) - 1))  // Unaligned pointer (very rare)
        std::memcpy(&v, addr, sizeof(v));
    else
        v = *((T*) (addr));

    if (Endian != IsLittleEndian)
        swap_endian(v);
    return v;
}

// DTZ-tables don't store valid scores for moves that reset the rule50 counter
// like captures and pawn moves but can easily recover the correct DTZ-score of the
// previous move if know the position's WDL-score.
int before_zeroing_dtz(WDLScore wdlScore) noexcept {
    switch (wdlScore)
    {
    case WDL_BLESSED_LOSS :
        return -101;
    case WDL_LOSS :
        return -1;
    case WDL_WIN :
        return +1;
    case WDL_CURSED_WIN :
        return +101;
    default :
        return 0;
    }
}

// Numbers in little-endian used by sparseIndex[] to point into blockLength[]
struct SparseEntry final {
    char block[4];   // Number of block
    char offset[2];  // Offset within the block
};

static_assert(sizeof(SparseEntry) == 6, "SparseEntry must be 6 bytes");

using Sym = std::uint16_t;  // Huffman symbol

struct LR final {
    template<bool Left>
    constexpr Sym get() const noexcept {
        if constexpr (Left)
            return ((data[1] & 0xF) << 8) | data[0];
        else
            return (data[2] << 4) | (data[1] >> 4);
    }

    // First 12 bits is the left-hand symbol, second 12 bits is the right-hand symbol.
    // If the symbol has length 1, then the left-hand symbol is the stored value.
    StdArray<std::uint8_t, 3> data;
};

static_assert(sizeof(LR) == 3, "LR tree entry must be 3 bytes");

// Tablebases data layout is structured as following:
//
//  TBFile:   memory maps/unmaps the physical .rtbw and .rtbz files
//  TBTable:  one object for each file with corresponding indexing information
//  TBTables: has ownership of TBTable objects, keeping a list and a hash

// class TBFile memory maps/unmaps the single .rtbw and .rtbz files. Files are
// memory mapped for best performance. Files are mapped at first access: at init
// time only existence of the file is checked.
class TBFile: public std::ifstream {
   public:
    explicit TBFile(std::string_view file) noexcept {

        for (const auto& path : Paths)
        {
            filename = path + "/" + file.data();
            open(filename);
            if (is_open())
                return;
        }
    }

    // Memory map the file and check it
    template<TBType Type>
    std::uint8_t* map(void** baseAddress, std::uint64_t* mapping) noexcept {

        if (is_open())
            close();  // Need to re-open to get native file descriptor

#if !defined(_WIN32)
        int fd = ::open(filename.c_str(), O_RDONLY);

        if (fd == -1)
            return *baseAddress = nullptr, nullptr;

        struct stat st;
        fstat(fd, &st);

        if (st.st_size % 64 != 16)
        {
            std::cerr << "Corrupt tablebase file " << filename << std::endl;
            std::exit(EXIT_FAILURE);
        }

        *mapping     = st.st_size;
        *baseAddress = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    #if defined(MADV_RANDOM)
        madvise(*baseAddress, st.st_size, MADV_RANDOM);
    #endif
        ::close(fd);

        if (*baseAddress == MAP_FAILED)
        {
            std::cerr << "mmap() failed, name = " << filename << std::endl;
            std::exit(EXIT_FAILURE);
        }
#else
        // Note FILE_FLAG_RANDOM_ACCESS is only a hint to Windows and as such may get ignored
        HANDLE fd = CreateFile(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);

        if (fd == INVALID_HANDLE_VALUE)
            return *baseAddress = nullptr, nullptr;

        DWORD hiSize;
        DWORD loSize = GetFileSize(fd, &hiSize);

        if (loSize % 64 != 16)
        {
            std::cerr << "Corrupt tablebase file " << filename << std::endl;
            std::exit(EXIT_FAILURE);
        }

        HANDLE hMapFile = CreateFileMapping(fd, nullptr, PAGE_READONLY, hiSize, loSize, nullptr);
        CloseHandle(fd);

        if (hMapFile == nullptr)
        {
            std::cerr << "CreateFileMapping() failed, name = " << filename << std::endl;
            std::exit(EXIT_FAILURE);
        }

        *mapping     = std::uint64_t(hMapFile);
        *baseAddress = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0);

        if (!*baseAddress)
        {
            std::cerr << "MapViewOfFile() failed, name = " << filename
                      << ", error = " << GetLastError() << std::endl;
            std::exit(EXIT_FAILURE);
        }
#endif

        auto* data = (std::uint8_t*) (*baseAddress);

        constexpr std::size_t  MagicSize = 4;
        constexpr std::uint8_t Magics[2][MagicSize]{{0xD7, 0x66, 0x0C, 0xA5},
                                                    {0x71, 0xE8, 0x23, 0x5D}};

        if (std::memcmp(data, Magics[Type == WDL], MagicSize))
        {
            std::cerr << "Corrupted table in file " << filename << std::endl;
            unmap(*baseAddress, *mapping);
            return *baseAddress = nullptr, nullptr;
        }

        return data + MagicSize;  // Skip Magics's header
    }

    static void unmap(void* baseAddress, std::uint64_t mapping) noexcept {

#if !defined(_WIN32)
        munmap(baseAddress, mapping);
#else
        UnmapViewOfFile(baseAddress);
        CloseHandle((HANDLE) mapping);
#endif
    }

    static bool init(std::string_view paths) noexcept {
        // Multiple directories are separated
        // by ";" on Windows and
        // by ":" on Unix-based operating systems.
        //
        // Example:
        // C:\tb\wdl345;C:\tb\wdl6;D:\tb\dtz345;D:\tb\dtz6
        constexpr char PathSeparator =
#if defined(_WIN32)
          ';'
#else
          ':'
#endif
          ;

        Paths.clear();

        if (!paths.empty())
        {
            std::istringstream iss{std::string(paths)};
            std::string        path;
            while (std::getline(iss, path, PathSeparator))
                if (!path.empty())
                    Paths.push_back(path);
        }
        return !Paths.empty();
    }

    // Look for and open the file among the Paths directories
    // where the .rtbw and .rtbz files can be found.
    static Strings Paths;

   private:
    std::string filename;
};

Strings TBFile::Paths;

// struct PairsData contains low-level indexing information to access TB data.
// There are 8, 4, or 2 PairsData records for each TBTable, according to the type
// of table and if positions have pawns or not. It is populated at first access.
struct PairsData final {
    std::uint8_t   flags;        // Table flags, see enum TBFlag
    std::uint8_t   maxSymLen;    // Maximum length in bits of the Huffman symbols
    std::uint8_t   minSymLen;    // Minimum length in bits of the Huffman symbols
    std::uint32_t  blockCount;   // Number of blocks in the TB file
    std::size_t    blockSize;    // Block size in bytes
    std::size_t    span;         // About every span values there is a SparseIndex[] entry
    Sym*           lowestSym;    // lowestSym[l] is the symbol of length l with the lowest value
    LR*            btree;        // btree[sym] stores the left and right symbols that expand sym
    std::uint16_t* blockLength;  // Number of stored positions (minus one) for each block: 1..65536
    std::uint32_t
      blockLengthSize;  // Size of blockLength[] table: padded so it's bigger than blockCount
    SparseEntry*  sparseIndex;      // Partial indices into blockLength[]
    std::size_t   sparseIndexSize;  // Size of SparseIndex[] table
    std::uint8_t* data;             // Start of Huffman compressed data
    std::vector<std::uint64_t>
      base64;  // base64[l - minSymLen] is the 64bit-padded lowest symbol of length l
    std::vector<std::uint8_t>
      symLen;  // Number of values (-1) represented by a given Huffman symbol: 1..256
    StdArray<Piece, TBPieces> pieces;  // Position pieces: the order of pieces defines the groups
    StdArray<std::uint64_t, TBPieces + 1>
      groupIdx;  // Start index used for the encoding of the group's pieces
    StdArray<std::int32_t, TBPieces + 1>
      groupLen;  // Number of pieces in a given group: KRKN -> (3, 1)
    StdArray<std::uint16_t, 4>
      mapIdx;  // WDLWin, WDLLoss, WDLCursedWin, WDLBlessedLoss (used in DTZ)
};

// struct TBTable contains indexing information to access the corresponding TBFile.
// There are 2 types of TBTable, corresponding to a WDL or a DTZ file. TBTable
// is populated at init time but the nested PairsData records are populated at
// first access, when the corresponding file is memory mapped.
template<TBType Type>
struct TBTable final {
    using Ret = std::conditional_t<Type == WDL, WDLScore, int>;

    static constexpr std::size_t Sides = Type == WDL ? 2 : 1;

    std::atomic<bool> ready{false};
    void*             baseAddress = nullptr;
    std::uint8_t*     map;
    std::uint64_t     mapping;

    StdArray<Key, COLOR_NB>                 key;
    std::uint8_t                            pieceCount;
    bool                                    hasPawns;
    bool                                    hasUniquePieces;
    StdArray<std::uint8_t, COLOR_NB>        pawnCount;  // [Lead color / other color]
    StdArray<PairsData, Sides, FILE_NB / 2> items;      // [wtm / btm][FILE_A..FILE_D or 0]

    PairsData* get(int ac, int f) noexcept { return &items[ac % Sides][hasPawns ? f : 0]; }

    TBTable() noexcept = default;
    explicit TBTable(std::string_view code) noexcept;
    explicit TBTable(const TBTable<WDL>& wdlTable) noexcept;

    ~TBTable() noexcept {
        if (baseAddress != nullptr)
            TBFile::unmap(baseAddress, mapping);
    }
};

template<>
TBTable<WDL>::TBTable(std::string_view code) noexcept :
    TBTable() {

    State st;

    Position pos;

    pos.set(code, WHITE, &st);
    key[WHITE] = pos.material_key();
    pieceCount = pos.count();
    hasPawns   = pos.count<PAWN>() != 0;

    hasUniquePieces = false;
    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
            if (pos.count(c, pt) == 1 && pt != KING)
                hasUniquePieces = true;

    // Set the leading color. In case both sides have pawns the leading color
    // is the side with fewer pawns because this leads to better compression.
    bool c = pos.count<PAWN>(BLACK) == 0
          || (pos.count<PAWN>(WHITE) != 0 && pos.count<PAWN>(BLACK) >= pos.count<PAWN>(WHITE));

    pawnCount[WHITE] = pos.count<PAWN>(c ? WHITE : BLACK);
    pawnCount[BLACK] = pos.count<PAWN>(c ? BLACK : WHITE);

    pos.set(code, BLACK, &st);
    key[BLACK] = pos.material_key();
}

template<>
TBTable<DTZ>::TBTable(const TBTable<WDL>& wdlTable) noexcept :
    TBTable() {

    // Use the corresponding WDL table to avoid recalculating all from scratch
    key[WHITE]       = wdlTable.key[WHITE];
    key[BLACK]       = wdlTable.key[BLACK];
    pieceCount       = wdlTable.pieceCount;
    hasPawns         = wdlTable.hasPawns;
    hasUniquePieces  = wdlTable.hasUniquePieces;
    pawnCount[WHITE] = wdlTable.pawnCount[WHITE];
    pawnCount[BLACK] = wdlTable.pawnCount[BLACK];
}

// class TBTables creates and keeps ownership of the TBTable objects,
// one for each TB file found. It supports a fast, hash-based, table lookup.
// Populated at init time, accessed at probe time.
class TBTables final {

    struct Entry final {
        template<TBType Type>
        TBTable<Type>* get() const noexcept {
            if constexpr (Type == WDL)
                return wdlTable;
            else
                return dtzTable;
        }

        Key           key;
        TBTable<WDL>* wdlTable;
        TBTable<DTZ>* dtzTable;
    };

    static constexpr std::size_t index(Key key) noexcept { return key & (Size - 1); }

    void insert(Key key, TBTable<WDL>* wdlTable, TBTable<DTZ>* dtzTable) noexcept {
        Entry entry{key, wdlTable, dtzTable};

        auto homeBucket = index(key);
        // Ensure last element is empty to avoid overflow when looking up
        for (auto bucket = homeBucket; bucket < Size + Overflow - 1; ++bucket)
        {
            Key otherKey = entries[bucket].key;
            if (otherKey == key || !entries[bucket].get<WDL>())
            {
                entries[bucket] = entry;
                return;
            }

            // Robin Hood hashing: If probed for longer than this element,
            // insert here and search for a new spot for the other element instead.
            auto otherHomeBucket = index(otherKey);
            if (homeBucket < otherHomeBucket)
            {
                homeBucket = otherHomeBucket;
                key        = otherKey;
                std::swap(entry, entries[bucket]);
            }
        }

        std::cerr << "TB hash table size too low!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // 4K table, indexed by key's 12 lsb
    static constexpr std::size_t Size = 1 << 12;
    // Number of elements allowed to map to the last bucket
    static constexpr std::size_t Overflow = 1;

    StdArray<Entry, Size + Overflow> entries;

    std::deque<TBTable<WDL>> wdlTables;
    std::deque<TBTable<DTZ>> dtzTables;

    std::size_t wdlCount = 0;
    std::size_t dtzCount = 0;

   public:
    template<TBType Type>
    TBTable<Type>* get(Key key) noexcept {
        for (const Entry* entry = &entries[index(key)];; ++entry)
            if (entry->key == key || !entry->get<Type>())
                return entry->get<Type>();
    }

    void clear() noexcept {
        //std::memset(entries.data(), 0, sizeof(entries));
        entries.fill({});
        wdlTables.clear();
        dtzTables.clear();
        wdlCount = 0;
        dtzCount = 0;
    }

    std::string info() const noexcept {
        return "Tablebase: "                            //
             + std::to_string(wdlCount) + " WDL and "   //
             + std::to_string(dtzCount) + " DTZ found"  //
             + " (up to " + std::to_string(int(MaxCardinality)) + "-man).";
    }

    void add(const std::vector<PieceType>& pieces) noexcept;
};

// If the corresponding file exists two new objects TBTable<WDL> and TBTable<DTZ>
// are created and added to the lists and hash table. Called at init time.
void TBTables::add(const std::vector<PieceType>& pieces) noexcept {

    std::string code;
    code.reserve(pieces.size() + 2);
    for (PieceType pt : pieces)
        code += to_char(pt);
    std::size_t pos = code.find('K', 1);
    assert(!code.empty() && code[0] == 'K' && pos != std::string::npos);
    if (pos == std::string::npos)
        return;
    code.insert(pos, 1, 'v');  // KRK -> KRvK

    TBFile dtzFile(code + DTZExt.data());
    if (dtzFile.is_open())
    {
        dtzFile.close();
        ++dtzCount;
    }

    TBFile wdlFile(code + WDLExt.data());
    if (!wdlFile.is_open())  // Only WDL file is checked
        return;

    wdlFile.close();
    ++wdlCount;

    MaxCardinality = std::max<std::size_t>(MaxCardinality, pieces.size());

    wdlTables.emplace_back(code);
    dtzTables.emplace_back(wdlTables.back());

    // Insert into the hash keys for both colors: KRvK with KR white and black
    insert(wdlTables.back().key[WHITE], &wdlTables.back(), &dtzTables.back());
    insert(wdlTables.back().key[BLACK], &wdlTables.back(), &dtzTables.back());
}

TBTables tbTables;

// TB-tables are compressed with canonical Huffman code. The compressed data is divided into
// blocks of size d->blockSize, and each block stores a variable number of symbols.
// Each symbol represents either a WDL or a (remapped) DTZ value, or a pair of other symbols
// (recursively). If you keep expanding the symbols in a block, you end up with up to 65536
// WDL or DTZ values. Each symbol represents up to 256 values and will correspond after
// Huffman coding to at least 1 bit. So a block of 32 bytes corresponds to at most
// 32 x 8 x 256 = 65536 values. This maximum is only reached for tables that consist mostly
// of draws or mostly of wins, but such tables are actually quite common. In principle, the
// blocks in WDL-tables are 64 bytes long (and will be aligned on cache lines). But for
// mostly-draw or mostly-win tables this can leave many 64-byte blocks only half-filled, so
// in such cases blocks are 32 bytes long. The blocks of DTZ-tables are up to 1024 bytes long.
// The generator picks the size that leads to the smallest table. The "book" of symbols and
// Huffman codes are the same for all blocks in the table. A non-symmetric pawn less TB file
// will have one table for wtm and one for btm, a TB file with pawns will have tables per
// file a,b,c,d also, in this case, one set for wtm and one for btm.
int decompress_pairs(PairsData* pd, std::uint64_t idx) noexcept {

    // Special case where all table positions store the same value
    if (pd->flags & SingleValue)
        return pd->minSymLen;

    // First need to locate the right block that stores the value at index "idx".
    // Because each block n stores blockLength[n] + 1 values, the index i of the block
    // that contains the value at position idx is:
    //
    //       for (i = -1, sum = 0; sum <= idx; ++i)
    //           sum += blockLength[i + 1] + 1;
    //
    // This can be slow, so use SparseIndex[] populated with a set of SparseEntry that
    // point to known indices into blockLength[]. Namely SparseIndex[k] is a SparseEntry
    // that stores the blockLength[] index and the offset within that block of the value
    // with index I(k), where:
    //
    //       I(k) = k * d->span + d->span / 2       (1)

    // First step is to get the 'k' of the I(k) nearest to our idx, using definition (1)
    auto k = std::uint32_t(idx / pd->span);

    // Then read the corresponding SparseIndex[] entry
    auto block  = number<std::uint32_t, Little>(&pd->sparseIndex[k].block);
    int  offset = number<std::uint16_t, Little>(&pd->sparseIndex[k].offset);

    // Now compute the difference idx - I(k). From the definition of k,
    //
    //       idx = k * d->span + idx % d->span      (2)
    //
    // So from (1) and (2) can compute idx - I(K):
    int diff = idx % pd->span - pd->span / 2;

    // Sum the above to offset to find the offset corresponding to our idx
    offset += diff;

    // Move to the previous/next block, until reach the correct block that contains idx,
    // that is when 0 <= offset <= d->blockLength[block]
    while (offset < 0)
        offset += pd->blockLength[--block] + 1;

    while (offset > pd->blockLength[block])
        offset -= pd->blockLength[block++] + 1;

    // Finally, find the start address of our block of canonical Huffman symbols
    auto* ptr = (std::uint32_t*) (pd->data + (std::uint64_t(block) * pd->blockSize));

    // Read the first 64 bits in our block, this is a (truncated) sequence of
    // unknown number of symbols of unknown length but the first one
    // is at the beginning of this 64-bit sequence.
    auto buf64     = number<std::uint64_t, Big>(ptr);
    int  buf64Size = 64;
    ptr += 2;
    Sym sym;

    while (true)
    {
        std::size_t len = 0;  // This is the symbol length - d->min_sym_len

        // Now get the symbol length.
        // For any symbol s64 of length 'l' right-padded to 64 bits that
        // d->base64[l-1] >= s64 >= d->base64[l]
        // so can find the symbol length iterating through base64[].
        while (buf64 < pd->base64[len])
            ++len;

        // All the symbols of a given length are consecutive integers (numerical sequence property),
        // so can compute the offset of our symbol of length len, stored at the beginning of buf64.
        sym = Sym((buf64 - pd->base64[len]) >> (64 - len - pd->minSymLen));

        // Now add the value of the lowest symbol of length len to get our symbol
        sym += number<Sym, Little>(&pd->lowestSym[len]);

        // If our offset is within the number of values represented by symbol 'sym', are done.
        if (offset < pd->symLen[sym] + 1)
            break;

        // ...otherwise update the offset and continue to iterate
        offset -= pd->symLen[sym] + 1;
        len += pd->minSymLen;  // Get the real length
        buf64 <<= len;         // Consume the just processed symbol
        buf64Size -= len;

        // Refill the buffer
        if (buf64Size <= 32)
        {
            buf64Size += 32;
            buf64 |= std::uint64_t(number<std::uint32_t, Big>(ptr++)) << (64 - buf64Size);
        }
    }

    // Now have our symbol that expands into d->symLen[sym] + 1 symbols.
    // Binary-search for our value recursively expanding into the left and
    // right child symbols until reach a leaf node where symLen[sym] + 1 == 1
    // that will store the value need.
    while (pd->symLen[sym])
    {
        Sym lSym = pd->btree[sym].get<true>();

        // If a symbol contains 36 sub-symbols (d->symLen[sym] + 1 = 36) and
        // expands in a pair (d->symLen[lSym] = 23, d->symLen[rSym] = 11), then
        // for instance, the tenth value (offset = 10) will be on the left side
        // because in Recursive Pairing child symbols are adjacent.
        if (offset < pd->symLen[lSym] + 1)
            sym = lSym;
        else
        {
            offset -= pd->symLen[lSym] + 1;
            sym = pd->btree[sym].get<false>();
        }
    }

    return pd->btree[sym].get<true>();
}

bool check_ac(TBTable<WDL>*, int, File) noexcept { return true; }

bool check_ac(TBTable<DTZ>* entry, int ac, File f) noexcept {
    return (entry->get(ac, f)->flags & AC) == ac
        || (!entry->hasPawns && entry->key[WHITE] == entry->key[BLACK]);
}

// DTZ scores are sorted by frequency of occurrence and
// then assigned the values 0, 1, 2, ... in order of decreasing frequency.
// This is done for each of the four WDLScore values.
// The mapping information necessary to reconstruct the original values
// are stored in the TB file and read during map[] init.
WDLScore map_score(TBTable<WDL>*, File, int value, WDLScore) noexcept {
    return WDLScore(value - 2);
}

int map_score(TBTable<DTZ>* entry, File f, int value, WDLScore wdlScore) noexcept {

    auto* pd     = entry->get(0, f);
    auto  flags  = pd->flags;
    auto* map    = entry->map;
    auto* mapIdx = pd->mapIdx.data();

    auto idx = mapIdx[WDLMap[wdlScore + 2]] + value;

    if (flags & Mapped)
        value = (flags & Wide) ? ((std::uint16_t*) map)[idx] : map[idx];

    // DTZ-tables store distance to zero in number of moves or plies.
    // So have to convert to plies when needed.
    if ((wdlScore == WDL_WIN && !(flags & WinPlies))       //
        || (wdlScore == WDL_LOSS && !(flags & LossPlies))  //
        || wdlScore == WDL_CURSED_WIN || wdlScore == WDL_BLESSED_LOSS)
        value *= 2;

    return value + 1;
}

// A temporary fix for the compiler bug with AVX-512
#if defined(USE_AVX512)
    #if defined(__clang__) && defined(__clang_major__) && __clang_major__ >= 15
        #define CLANG_AVX512_BUG_FIX __attribute__((optnone))
    #endif
#endif

#if !defined(CLANG_AVX512_BUG_FIX)
    #define CLANG_AVX512_BUG_FIX
#endif

// Compute a unique index out of a position and use it to probe the TB file.
// To encode k pieces of the same type and color, first sort the pieces by square
// in ascending order s1 <= s2 <= ... <= sk then compute the unique index as:
//
//      idx = Binomial[1][s1] + Binomial[2][s2] + ... + Binomial[k][sk]
//
template<typename T, typename Ret = typename T::Ret>
CLANG_AVX512_BUG_FIX Ret do_probe_table(
  const Position& pos, Key materialKey, T* entry, WDLScore wdlScore, ProbeState* ps) noexcept {

    // A given TB entry like KRK has associated two material keys: KRvk and Kvkr.
    // If both sides have the same pieces keys are equal. In this case TB-tables
    // only stores the 'white to move' case, so if the position to lookup has black
    // to move, need to switch the color and flip the squares before to lookup.
    bool blackSymmetric = pos.active_color() == BLACK && entry->key[WHITE] == entry->key[BLACK];

    // TB files are calculated for white as the stronger side. For instance, we
    // have KRvK, not KvKR. A position where the stronger side is white will have
    // its material key == entry->key[WHITE], otherwise have to switch the color
    // and flip the squares before to lookup.
    bool blackStronger = materialKey != entry->key[WHITE];

    bool flip = (blackSymmetric || blackStronger);

    int activeColor = flip ? ~pos.active_color() : pos.active_color();

    StdArray<Square, TBPieces> squares{};
    StdArray<Piece, TBPieces>  pieces;

    Bitboard    leadPawns   = 0;
    std::size_t leadPawnCnt = 0;

    std::size_t size   = 0;
    File        tbFile = FILE_A;

    // For pawns, TB files store 4 separate tables according if leading pawn is on
    // file a, b, c or d after reordering. The leading pawn is the one with maximum
    // PawnsMap[] value, that is the one most toward the edges and with lowest rank.
    if (entry->hasPawns)
    {
        // In all the 4 tables, pawns are at the beginning of the piece sequence and
        // their color is the reference one. So just pick the first one.
        Piece pc = entry->get(0, 0)->pieces[0];
        if (flip)
            pc = flip_color(pc);

        assert(type_of(pc) == PAWN);

        Bitboard b = leadPawns = pos.pieces(color_of(pc), PAWN);
        while (b)
        {
            Square s = pop_lsb(b);

            squares[size] = flip ? flip_rank(s) : s;
            ++size;
        }

        leadPawnCnt = size;

        std::swap(squares[0],
                  *std::max_element(squares.begin(), squares.begin() + leadPawnCnt, pawns_comp));

        tbFile = fold_to_edge(file_of(squares[0]));
    }

    // DTZ-tables are one-sided, i.e. they store positions only for white to
    // move or only for black to move, so check for side to move to be ac,
    // early exit otherwise.
    if (!check_ac(entry, activeColor, tbFile))
        return *ps = PS_AC_CHANGED, Ret();

    // Now ready to get all the position pieces (but the lead pawns)
    // and directly map them to the correct square and color.
    Bitboard b = pos.pieces() ^ leadPawns;
    while (b)
    {
        Square s  = pop_lsb(b);
        Piece  pc = pos[s];

        squares[size] = flip ? flip_rank(s) : s;
        pieces[size]  = flip ? flip_color(pc) : pc;
        ++size;
    }

    assert(size >= 2);

    PairsData* pd = entry->get(activeColor, tbFile);

    // Then reorder the pieces to have the same sequence as the one stored
    // in pieces[i]: the sequence that ensures the best compression.
    for (std::size_t i = leadPawnCnt; i < size - 1; ++i)
        for (std::size_t j = i + 1; j < size; ++j)
            if (pd->pieces[i] == pieces[j])
            {
                std::swap(pieces[i], pieces[j]);
                std::swap(squares[i], squares[j]);
                break;
            }

    // Now map again the squares so that the square of the lead piece is in
    // the triangle A1-D1-D4.
    if (file_of(squares[0]) > FILE_D)
        for (std::size_t i = 0; i < size; ++i)
            squares[i] = flip_file(squares[i]);

    std::uint64_t idx = 0;
    // Encode leading pawns starting with the one with minimum PawnsMap[] and
    // proceeding in ascending order.
    if (entry->hasPawns)
    {
        idx = LeadPawnIdx[leadPawnCnt][squares[0]];

        std::stable_sort(squares.begin() + 1, squares.begin() + leadPawnCnt, pawns_comp);

        for (std::size_t i = 1; i < leadPawnCnt; ++i)
            idx += Binomial[i][PawnsMap[squares[i]]];

        goto ENCODE_END;  // With pawns have finished special treatments
    }

    // In positions without pawns, further flip the squares to ensure leading
    // piece is below RANK_5.
    if (rank_of(squares[0]) > RANK_4)
        for (std::size_t i = 0; i < size; ++i)
            squares[i] = flip_rank(squares[i]);

    // Look for the first piece of the leading group not on the A1-D4 diagonal
    // and ensure it is mapped below the diagonal.
    for (std::int32_t i = 0; i < pd->groupLen[0]; ++i)
    {
        if (!off_A1H8(squares[i]))
            continue;

        if (off_A1H8(squares[i]) > 0)  // A1-H8 diagonal flip: SQ_A3 -> SQ_C1
            for (std::size_t j = i; j < size; ++j)
                squares[j] = Square(((squares[j] >> 3) | (squares[j] << 3)) & 0x3F);
        break;
    }

    // Encode the leading group.
    //
    // Suppose have KRvK. Let's say the pieces are on square numbers wK, wR
    // and bK (each 0...63). The simplest way to map this position to an index
    // is like this:
    //
    //   index = wK * 64 * 64 + wR * 64 + bK;
    //
    // But this way the TB is going to have 64*64*64 = 262144 positions, with
    // lots of positions being equivalent (because they are mirrors of each
    // other) and lots of positions being invalid (two pieces on one square,
    // adjacent kings, etc.).
    // Usually the first step is to take the wK and bK together. There are just
    // 462 ways legal and not-mirrored ways to place the wK and bK on the board.
    // Once have placed the wK and bK, there are 62 squares left for the wR
    // Mapping its square from 0..63 to available squares 0..61 can be done like:
    //
    //   wR -= (wR > wK) + (wR > bK);
    //
    // In words: if wR "comes later" than wK, deduct 1, and the same if wR
    // "comes later" than bK. In case of two same pieces like KRRvK want to
    // place the two Rs "together". If have 62 squares left, can place two
    // Rs "together" in 62 * 61 / 2 ways (divide by 2 because rooks can be
    // swapped and still get the same position.)
    //
    // In case have at least 3 unique pieces (including kings) encode them together.
    if (entry->hasUniquePieces)
    {
        int adjust1 = (squares[1] > squares[0]);
        int adjust2 = (squares[2] > squares[0]) + (squares[2] > squares[1]);

        // First piece is below a1-h8 diagonal. A1D1D4Map[] maps the b1-d1-d3
        // triangle to 0...5. There are 63 squares for second piece and 62
        // (mapped to 0...61) for the third.
        if (off_A1H8(squares[0]))
            idx = (A1D1D4Map[squares[0]] * 63 + (squares[1] - adjust1)) * 62 + squares[2] - adjust2;

        // First piece is on a1-h8 diagonal, second below: map this occurrence to
        // 6 to differentiate from the above case, rank_of() maps a1-d4 diagonal
        // to 0...3 and finally B1H1H7Map[] maps the b1-h1-h7 triangle to 0..27.
        else if (off_A1H8(squares[1]))
            idx = (6 * 63 + rank_of(squares[0]) * 28 + B1H1H7Map[squares[1]]) * 62 + squares[2]
                - adjust2;

        // First two pieces are on a1-h8 diagonal, third below
        else if (off_A1H8(squares[2]))
            idx = 6 * 63 * 62 + 4 * 28 * 62 + rank_of(squares[0]) * 7 * 28
                + (rank_of(squares[1]) - adjust1) * 28 + B1H1H7Map[squares[2]];

        // All 3 pieces on the diagonal a1-h8
        else
            idx = 6 * 63 * 62 + 4 * 28 * 62 + 4 * 7 * 28 + rank_of(squares[0]) * 7 * 6
                + (rank_of(squares[1]) - adjust1) * 6 + (rank_of(squares[2]) - adjust2);
    }
    else
        // Don't have at least 3 unique pieces, like in KRRvKBB, just map the kings.
        idx = KKMap[A1D1D4Map[squares[0]]][squares[1]];

ENCODE_END:
    idx *= pd->groupIdx[0];

    Square* groupSq = squares.data() + pd->groupLen[0];

    // Encode remaining pawns and then pieces according to square, in ascending order
    bool pawnsRemaining = entry->hasPawns && entry->pawnCount[BLACK];

    std::size_t next = 0;
    while (pd->groupLen[++next])
    {
        std::stable_sort(groupSq, groupSq + pd->groupLen[next]);

        std::uint64_t n = 0;
        // Map down a square if "comes later" than a square in the previous
        // groups (similar to what was done earlier for leading group pieces).
        for (std::int32_t i = 0; i < pd->groupLen[next]; ++i)
        {
            auto adjust =
              std::count_if(squares.data(), groupSq, [&](Square s) { return groupSq[i] > s; });
            n += Binomial[i + 1][int(groupSq[i]) - adjust - 8 * pawnsRemaining];
        }

        pawnsRemaining = false;
        idx += n * pd->groupIdx[next];
        groupSq += pd->groupLen[next];
    }

    // Now that have the index, decompress the pair and get the WDL-score
    return map_score(entry, tbFile, decompress_pairs(pd, idx), wdlScore);
}

#undef CLANG_AVX512_BUG_FIX

// Group together pieces that will be encoded together. The general rule is that
// a group contains pieces of the same type and color. The exception is the leading
// group that, in case of positions without pawns, can be formed by 3 different
// pieces (default) or by the king pair when there is not a unique piece apart
// from the kings. When there are pawns, pawns are always first in pieces[].
//
// As example KRKN -> KRK + N, KNNK -> KK + NN, KPPKP -> P + PP + K + K
//
// The actual grouping depends on the TB generator and can be inferred from the
// sequence of pieces in piece[] array.
template<typename T>
void set_groups(T& entry, PairsData* pd, int order[2], File f) noexcept {

    std::size_t n = 0;

    pd->groupLen[n] = 1;

    std::int32_t firstLen = entry.hasPawns ? 0 : entry.hasUniquePieces ? 3 : 2;
    // Number of pieces per group is stored in groupLen[], for instance in KRKN
    // the encoder will default on '111', so groupLen[] will be (3, 1).
    for (std::int32_t i = 1; i < entry.pieceCount; ++i)
        if (--firstLen > 0 || pd->pieces[i] == pd->pieces[i - 1])
            pd->groupLen[n]++;
        else
            pd->groupLen[++n] = 1;
    pd->groupLen[++n] = 0;  // Zero-terminated

    // The sequence in pieces[] defines the groups, but not the order in which
    // they are encoded. If the pieces in a group g can be combined on the board
    // in N(g) different ways, then the position encoding will be of the form:
    //
    //       g1 * N(g2) * N(g3) + g2 * N(g3) + g3
    //
    // This ensures unique encoding for the whole position. The order of the
    // groups is a per-table parameter and could not follow the canonical leading
    // pawns/pieces -> remaining pawns -> remaining pieces. In particular the
    // first group is at order[0] position and the remaining pawns, when present,
    // are at order[1] position.
    bool        pp      = entry.hasPawns && entry.pawnCount[BLACK] != 0;  // Pawns on both sides
    std::size_t next    = pp ? 2 : 1;
    std::size_t freeLen = 64 - pd->groupLen[0] - (pp ? pd->groupLen[1] : 0);

    std::uint64_t idx = 1;

    for (int k = 0; k == order[0] || k == order[1] || next < n; ++k)
        // Leading pawns or pieces
        if (k == order[0])
        {
            pd->groupIdx[0] = idx;
            idx *= entry.hasPawns        ? LeadPawnSize[pd->groupLen[0]][f]
                 : entry.hasUniquePieces ? 31332
                                         : 462;
        }
        // Remaining pawns
        else if (k == order[1])
        {
            pd->groupIdx[1] = idx;
            idx *= Binomial[pd->groupLen[1]][48 - pd->groupLen[0]];
        }
        // Remaining pieces
        else
        {
            pd->groupIdx[next] = idx;
            idx *= Binomial[pd->groupLen[next]][freeLen];
            assert(int(freeLen) >= pd->groupLen[next]);
            freeLen -= pd->groupLen[next];
            ++next;
        }

    pd->groupIdx[n] = idx;
}

// In Recursive Pairing each symbol represents a pair of children symbols. So
// read d->btree[] symbols data and expand each one in his left and right child
// symbol until reaching the leaves that represent the symbol value.
std::uint8_t set_symlen(PairsData* pd, Sym s, std::vector<bool>& visited) noexcept {

    visited[s] = true;  // Can set it now because tree is acyclic

    Sym rSym = pd->btree[s].get<false>();

    if (rSym == 0xFFF)
        return 0;

    Sym lSym = pd->btree[s].get<true>();

    if (!visited[lSym])
        pd->symLen[lSym] = set_symlen(pd, lSym, visited);

    if (!visited[rSym])
        pd->symLen[rSym] = set_symlen(pd, rSym, visited);

    return 1 + pd->symLen[lSym] + pd->symLen[rSym];
}

std::uint8_t* set_sizes(PairsData* pd, std::uint8_t* data) noexcept {

    pd->flags = *data++;

    if (pd->flags & SingleValue)
    {
        pd->blockCount      = 0;
        pd->blockLengthSize = 0;
        pd->span            = 0;
        pd->sparseIndexSize = 0;        // Broken MSVC zero-init
        pd->minSymLen       = *data++;  // Here store the single value
        return data;
    }

    // groupLen[] is a zero-terminated list of group lengths, the last groupIdx[]
    // element stores the biggest index that is the tb size.
    std::uint64_t tbSize =
      pd->groupIdx[std::find(pd->groupLen.begin(), pd->groupLen.end(), 0) - pd->groupLen.begin()];

    pd->blockSize       = 1ULL << *data++;
    pd->span            = 1ULL << *data++;
    pd->sparseIndexSize = (tbSize + pd->span - 1) / pd->span;  // Round up

    auto padding = number<std::uint8_t, Little>(data);
    data += 1;
    pd->blockCount = number<std::uint32_t, Little>(data);
    data += sizeof(std::uint32_t);
    // Padded to ensure SparseIndex[] does not point out of range.
    pd->blockLengthSize = pd->blockCount + padding;
    pd->maxSymLen       = *data++;
    pd->minSymLen       = *data++;
    pd->lowestSym       = (Sym*) (data);
    pd->base64.resize(pd->maxSymLen - pd->minSymLen + 1);

    // See https://en.wikipedia.org/wiki/Huffman_coding
    // The canonical code is ordered such that longer symbols (in terms of
    // the number of bits of their Huffman code) have a lower numeric value,
    // so that d->lowestSym[i] >= d->lowestSym[i+1] (when read as LittleEndian).
    // Starting from this compute a base64[] table indexed by symbol length
    // and containing 64 bit values so that d->base64[i] >= d->base64[i+1].

    // Implementation note: first cast the unsigned size_t "base64.size()"
    // to a signed int "base64_size" variable and then are able to subtract 2,
    // avoiding unsigned overflow warnings.

    std::size_t base64Size = pd->base64.size();
    for (std::size_t i = base64Size ? base64Size - 1 : 0; i-- > 0;)
    {
        pd->base64[i] = (pd->base64[i + 1]                         //
                         + number<Sym, Little>(&pd->lowestSym[i])  //
                         - number<Sym, Little>(&pd->lowestSym[i + 1]))
                      / 2;

        assert(2 * pd->base64[i] >= pd->base64[i + 1]);
    }

    // Now left-shift by an amount so that d->base64[i] gets shifted 1 bit more
    // than d->base64[i+1] and given the above assert condition, ensure that
    // d->base64[i] >= d->base64[i+1]. Moreover for any symbol s64 of length i
    // and right-padded to 64 bits holds d->base64[i-1] >= s64 >= d->base64[i].
    for (std::size_t i = 0; i < base64Size; ++i)
        pd->base64[i] <<= 64 - i - pd->minSymLen;  // Right-padding to 64 bits

    data += base64Size * sizeof(Sym);
    pd->symLen.resize(number<std::uint16_t, Little>(data));
    data += sizeof(std::uint16_t);
    pd->btree = (LR*) (data);

    // The compression scheme used is "Recursive Pairing", that replaces the most
    // frequent adjacent pair of symbols in the source message by a new symbol,
    // reevaluating the frequencies of all of the symbol pairs with respect to
    // the extended alphabet, and then repeating the process.
    // See https://web.archive.org/web/20201106232444/http://www.larsson.dogma.net/dcc99.pdf
    std::vector<bool> visited(pd->symLen.size());
    for (std::size_t s = 0; s < pd->symLen.size(); ++s)
        if (!visited[s])
            pd->symLen[s] = set_symlen(pd, s, visited);

    return data + pd->symLen.size() * sizeof(LR) + (pd->symLen.size() & 1);
}

std::uint8_t* set_dtz_map(TBTable<WDL>&, std::uint8_t* data, File) noexcept { return data; }

std::uint8_t* set_dtz_map(TBTable<DTZ>& entry, std::uint8_t* data, File maxFile) noexcept {

    entry.map = data;

    for (File f = FILE_A; f <= maxFile; ++f)
    {
        auto* pd = entry.get(0, f);

        auto flags = pd->flags;
        if (flags & Mapped)
        {
            if (flags & Wide)
            {
                data += std::uintptr_t(data) & 1;  // Word alignment, may have a mixed table
                for (std::size_t i = 0; i < 4; ++i)
                {
                    // Sequence like 3,x,x,x,1,x,0,2,x,x
                    pd->mapIdx[i] = 1 + (std::uint16_t*) (data) - (std::uint16_t*) (entry.map);
                    data += 2 + 2 * number<std::uint16_t, Little>(data);
                }
            }
            else
            {
                for (std::size_t i = 0; i < 4; ++i)
                {
                    pd->mapIdx[i] = 1 + data - entry.map;
                    data += 1 + *data;
                }
            }
        }
    }

    return data += std::uintptr_t(data) & 1;  // Word alignment
}

// Populate entry's PairsData records with data from the just memory-mapped file.
// Called at first access.
template<typename T>
void set(T& entry, std::uint8_t* data) noexcept {

    assert((entry.key[WHITE] != entry.key[BLACK]) == bool(*data & 1));
    assert(entry.hasPawns == bool(*data & 2));

    ++data;  // First byte stores flags

    const std::size_t sides   = T::Sides == 2 && entry.key[WHITE] != entry.key[BLACK] ? 2 : 1;
    const File        maxFile = entry.hasPawns ? FILE_D : FILE_A;

    bool pp = entry.hasPawns && entry.pawnCount[BLACK] != 0;  // Pawns on both sides

    assert(!pp || entry.pawnCount[WHITE] != 0);

    for (File f = FILE_A; f <= maxFile; ++f)
    {
        for (std::size_t i = 0; i < sides; ++i)
            *entry.get(i, f) = PairsData();

        int order[2][2]{{*data & 0xF, pp ? *(data + 1) & 0xF : 0xF},
                        {*data >> 4, pp ? *(data + 1) >> 4 : 0xF}};
        data += 1 + pp;

        for (std::uint8_t k = 0; k < entry.pieceCount; ++k, ++data)
            for (std::size_t i = 0; i < sides; ++i)
                entry.get(i, f)->pieces[k] = Piece(i ? *data >> 4 : *data & 0xF);

        for (std::size_t i = 0; i < sides; ++i)
            set_groups(entry, entry.get(i, f), order[i], f);
    }

    data += std::uintptr_t(data) & 1;  // Word alignment

    for (File f = FILE_A; f <= maxFile; ++f)
        for (std::size_t i = 0; i < sides; ++i)
            data = set_sizes(entry.get(i, f), data);

    data = set_dtz_map(entry, data, maxFile);

    PairsData* pd;
    for (File f = FILE_A; f <= maxFile; ++f)
        for (std::size_t i = 0; i < sides; ++i)
        {
            (pd = entry.get(i, f))->sparseIndex = (SparseEntry*) (data);
            data += pd->sparseIndexSize * sizeof(SparseEntry);
        }

    for (File f = FILE_A; f <= maxFile; ++f)
        for (std::size_t i = 0; i < sides; ++i)
        {
            (pd = entry.get(i, f))->blockLength = (std::uint16_t*) (data);
            data += pd->blockLengthSize * sizeof(std::uint16_t);
        }

    for (File f = FILE_A; f <= maxFile; ++f)
        for (std::size_t i = 0; i < sides; ++i)
        {
            data = (std::uint8_t*) ((std::uintptr_t(data) + 0x3F) & ~0x3F);  // 64 byte alignment
            (pd = entry.get(i, f))->data = data;
            data += pd->blockCount * pd->blockSize;
        }
}

// If the TB file corresponding to the given position is already memory-mapped
// then return its base address, otherwise, try to memory map and init it.
// Called at every probe, memory map, and init only at first access.
// Function is thread safe and can be called concurrently.
template<TBType Type>
void* mapped(const Position& pos, Key materialKey, TBTable<Type>& entry) noexcept {
    static std::mutex mutex;

    // Use 'acquire' to avoid a thread reading 'ready' == true while
    // another is still working. (compiler reordering may cause this).
    if (entry.ready.load(std::memory_order_acquire))
        return entry.baseAddress;  // Could be nullptr if file does not exist

    std::scoped_lock scopedLock(mutex);

    if (entry.ready.load(std::memory_order_relaxed))  // Recheck under lock
        return entry.baseAddress;

    // Pieces strings in decreasing order for each color, like ("KPP","KR")
    StdArray<std::string, COLOR_NB> pieces{};
    for (Color c : {WHITE, BLACK})
        for (auto itr = PIECE_TYPES.rbegin(); itr != PIECE_TYPES.rend(); ++itr)
            pieces[c].append(pos.count(c, *itr), to_char(*itr));

    std::string fname = (materialKey == entry.key[WHITE] ? pieces[WHITE] + 'v' + pieces[BLACK]
                                                         : pieces[BLACK] + 'v' + pieces[WHITE])
                      + (Type == WDL ? WDLExt : DTZExt).data();

    uint8_t* data = TBFile(fname).map<Type>(&entry.baseAddress, &entry.mapping);

    if (data != nullptr)
        set(entry, data);

    entry.ready.store(true, std::memory_order_release);
    return entry.baseAddress;
}

template<TBType Type, typename Ret = typename TBTable<Type>::Ret>
Ret probe_table(const Position& pos, ProbeState* ps, WDLScore wdlScore = WDL_DRAW) noexcept {

    Key materialKey = pos.material_key();

    if (materialKey == 0)  // KvK, pos.count() == 2
        return Ret(WDL_DRAW);

    TBTable<Type>* entry = tbTables.get<Type>(materialKey);

    if (entry == nullptr || mapped(pos, materialKey, *entry) == nullptr)
        return *ps = PS_FAIL, Ret();

    return do_probe_table(pos, materialKey, entry, wdlScore, ps);
}

// For a position where the side to move has a winning capture it is not necessary
// to store a winning WDL-score so the generator treats such positions as "don't care"
// and tries to assign to it a WDL-score that improves the compression ratio. Similarly,
// if the side to move has a drawing capture, then the position is at least drawn.
// If the position is won, then the TB needs to store a win WDL-score. But if the
// position is drawn, the TB may store a loss WDL-score if that is better for compression.
// All of this means that during probing, the engine must look at captures and probe
// their results and must probe the position itself. The "best" result of these
// probes is the correct result for the position.
// DTZ-tables do not store scores when a following move is a zeroing winning move
// (winning capture or winning pawn move). Also, DTZ store wrong scores for positions
// where the best move is an ep-move (even if losing). So in all these cases set
// the state to PS_BEST_MOVE_ZEROING.
template<bool CheckZeroingMoves>
WDLScore search(Position& pos, ProbeState* ps) noexcept {

    WDLScore wdlScore, bestWdlScore = WDL_LOSS;

    const MoveList<LEGAL> legalMoveList(pos);
    std::uint8_t          moveCount = 0;

    for (auto m : legalMoveList)
    {
        if (!pos.capture(m) && (!CheckZeroingMoves || type_of(pos.moved_piece(m)) != PAWN))
            continue;

        ++moveCount;

        State st;
        pos.do_move(m, st);
        wdlScore = -search<false>(pos, ps);
        pos.undo_move(m);

        if (*ps == PS_FAIL)
            return WDL_DRAW;

        if (bestWdlScore < wdlScore)
        {
            bestWdlScore = wdlScore;

            if (wdlScore >= WDL_WIN)
            {
                // Winning DTZ-zeroing move
                return *ps = PS_BEST_MOVE_ZEROING, wdlScore;
            }
        }
    }

    // In case have already searched all the legal moves don't have to probe
    // the TB because the stored WDL-score could be wrong. For instance TB-tables
    // do not contain information on position with ep rights, so in this case
    // the result of probe_wdl_table is wrong. Also in case of only capture
    // moves, for instance here 4K3/4q3/6p1/2k5/6p1/8/8/8 w - - 0 7, have to
    // return with PS_BEST_MOVE_ZEROING set.
    bool movesNoMore = (moveCount != 0 && moveCount == legalMoveList.size());

    if (movesNoMore)
        wdlScore = bestWdlScore;
    else
    {
        wdlScore = probe_table<WDL>(pos, ps);

        if (*ps == PS_FAIL)
            return WDL_DRAW;
    }

    // DTZ stores a "don't care" WDL-score if best WDL-score is a win
    if (bestWdlScore >= wdlScore)
        return *ps = (bestWdlScore > WDL_DRAW || movesNoMore ? PS_BEST_MOVE_ZEROING : PS_OK),
               bestWdlScore;

    return *ps = PS_OK, wdlScore;
}

}  // namespace

namespace Tablebases {

// Called at startup to create the various tables
void init() noexcept {

    std::size_t code;

    // B1H1H7Map[] encodes a square below a1-h8 diagonal to 0..27
    code = 0;
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        if (off_A1H8(s) < 0)
            B1H1H7Map[s] = code++;

    // A1D1D4Map[] encodes a square in the a1-d1-d4 triangle to 0..9
    std::vector<Square> diagonal;
    code = 0;
    for (Square s = SQ_A1; s <= SQ_D4; ++s)
        if (file_of(s) <= FILE_D)
        {
            if (off_A1H8(s) < 0)
                A1D1D4Map[s] = code++;

            else if (!off_A1H8(s))
                diagonal.push_back(s);
        }

    // Diagonal squares are encoded as last ones
    for (Square s : diagonal)
        A1D1D4Map[s] = code++;

    // KKMap[] encodes all the 462 possible legal positions of two kings where
    // the first is in the a1-d1-d4 triangle. If the first king is on the a1-d4
    // diagonal, the other one shall not be above the a1-h8 diagonal.
    std::vector<std::pair<int, Square>> bothOnDiagonal;
    code = 0;
    for (std::size_t idx = 0; idx < 10; ++idx)
        for (Square s1 = SQ_A1; s1 <= SQ_D4; ++s1)
            if (A1D1D4Map[s1] == idx && (idx || s1 == SQ_B1))  // SQ_B1 is mapped to 0
            {
                for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
                    if ((attacks_bb<KING>(s1) | s1) & s2)
                        continue;  // Illegal position

                    else if (!off_A1H8(s1) && off_A1H8(s2) > 0)
                        continue;  // First on diagonal, second above

                    else if (!off_A1H8(s1) && !off_A1H8(s2))
                        bothOnDiagonal.emplace_back(idx, s2);

                    else
                        KKMap[idx][s2] = code++;
            }

    // Legal positions with both kings on a diagonal are encoded as last ones
    for (const auto& [idx, s] : bothOnDiagonal)
        KKMap[idx][s] = code++;

    // Binomial[] stores the Binomial Coefficients using Pascal rule.
    // There are Binomial[k][n] ways to choose k elements from a set of n elements.
    Binomial[0][0] = 1;
    for (std::size_t n = 1; n < SQUARE_NB; ++n)                         // Squares
        for (std::size_t k = 0; k <= std::min(n, std::size_t(5)); ++k)  // Pieces
            Binomial[k][n] =
              (k > 0 ? Binomial[k - 1][n - 1] : 0) + (k < n ? Binomial[k][n - 1] : 0);

    // PawnsMap[s] encodes squares a2-h7 to (0..47).
    // This is the number of possible available squares when the leading one is in 's'.
    // Moreover the pawn with highest PawnsMap[] is the leading pawn, the one nearest the edge,
    // and among pawns with the same file, the one with the lowest rank.
    code = 47;  // Available squares when lead pawn is in a2
    // Init the tables for the encoding of leading pawn group:
    // with 7-men TB can have up to 5 leading pawns (KPPPPPK).
    for (std::size_t leadPawnCnt = 1; leadPawnCnt <= 5; ++leadPawnCnt)
        for (File f = FILE_A; f <= FILE_D; ++f)
        {
            // Restart the index at every file because TB table is split
            // by file, so can reuse the same index for different files.
            std::size_t idx = 0;

            // Sum all possible combinations for a given file, starting with
            // the leading pawn on rank 2 and increasing the rank.
            for (Rank r = RANK_2; r <= RANK_7; ++r)
            {
                Square s = make_square(f, r);

                // Compute PawnsMap[] at first pass.
                // If sq is the leading pawn square, any other pawn cannot be
                // below or more toward the edge of sq. There are 47 available
                // squares when sq = a2 and reduced by 2 for any rank increase
                // due to mirroring: sq == a3 -> no a2, h2, so PawnsMap[a3] = 45
                if (leadPawnCnt == 1)
                {
                    PawnsMap[s]            = code--;
                    PawnsMap[flip_file(s)] = code--;
                }
                LeadPawnIdx[leadPawnCnt][s] = idx;
                idx += Binomial[leadPawnCnt - 1][PawnsMap[s]];
            }
            // After a file is traversed, store the cumulated per-file index
            LeadPawnSize[leadPawnCnt][f] = idx;
        }
}

// Called after every change to "SyzygyPath" UCI option
// to (re)create the various tables.
// It is not thread safe, nor it needs to be.
void init(std::string_view paths) noexcept {

    MaxCardinality = 0;
    tbTables.clear();

    if (!TBFile::init(paths))
        return;

    // Add entries in TB-tables if the corresponding WDL file exists
    for (PieceType p1 = PAWN; p1 < KING; ++p1)
    {
        tbTables.add({KING, p1, KING});

        for (PieceType p2 = PAWN; p2 <= p1; ++p2)
        {
            tbTables.add({KING, p1, p2, KING});
            tbTables.add({KING, p1, KING, p2});

            for (PieceType p3 = PAWN; p3 < KING; ++p3)
                tbTables.add({KING, p1, p2, KING, p3});

            for (PieceType p3 = PAWN; p3 <= p2; ++p3)
            {
                tbTables.add({KING, p1, p2, p3, KING});

                for (PieceType p4 = PAWN; p4 <= p3; ++p4)
                {
                    tbTables.add({KING, p1, p2, p3, p4, KING});

                    for (PieceType p5 = PAWN; p5 <= p4; ++p5)
                        tbTables.add({KING, p1, p2, p3, p4, p5, KING});

                    for (PieceType p5 = PAWN; p5 < KING; ++p5)
                        tbTables.add({KING, p1, p2, p3, p4, KING, p5});
                }

                for (PieceType p4 = PAWN; p4 < KING; ++p4)
                {
                    tbTables.add({KING, p1, p2, p3, KING, p4});

                    for (PieceType p5 = PAWN; p5 <= p4; ++p5)
                        tbTables.add({KING, p1, p2, p3, KING, p4, p5});
                }
            }

            for (PieceType p3 = PAWN; p3 <= p1; ++p3)
                for (PieceType p4 = PAWN; p4 <= (p1 == p3 ? p2 : p3); ++p4)
                    tbTables.add({KING, p1, p2, KING, p3, p4});
        }
    }

    UCI::print_info_string(tbTables.info());
}

// Probe the WDL table for a particular position.
// If *ps != FAIL, the probe was successful.
// The return WDL-score is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
WDLScore probe_wdl(Position& pos, ProbeState* ps) noexcept {

    *ps = PS_OK;
    return search<false>(pos, ps);
}

// Probe the DTZ table for a particular position.
// If *ps != FAIL, the probe was successful.
// The return WDL-score is from the point of view of the side to move:
//         n < -100 : loss, but draw under 50-move rule
// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
//        -1        : loss, the side to move is mated
//         0        : draw
//     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
//   100 < n        : win, but draw under 50-move rule
//
// The return WDL-score n can be off by 1: a return WDL-score -n can mean a loss
// in n+1 ply and a return WDL-score +n can mean a win in n+1 ply. This
// cannot happen for tables with positions exactly on the "edge" of
// the 50-move rule.
//
// This implies that if DTZ-score > 0 is returned, the position is certainly
// a win if DTZ-score + 50-move-counter < 100. Care must be taken that the engine
// picks moves that preserve DTZ-score + 50-move-counter < 100.
//
// If n = 100 immediately after a capture or pawn move, then the position
// is also certainly a win, and during the whole phase until the next
// capture or pawn move, the inequality to be preserved is
// DTZ-score + 50-move-counter <= 100.
//
// In short, if a move is available resulting in DTZ-score + 50-move-counter < 100,
// then do not accept moves leading to DTZ-score + 50-move-counter == 100.
int probe_dtz(Position& pos, ProbeState* ps) noexcept {

    *ps           = PS_OK;
    auto wdlScore = search<true>(pos, ps);

    if (*ps == PS_FAIL || wdlScore == WDL_DRAW)  // DTZ-tables don't store draws
        return 0;

    // DTZ stores a 'don't care value in this case, or even a plain wrong
    // one as in case the best move is a losing ep, so it cannot be probed.
    if (*ps == PS_BEST_MOVE_ZEROING)
        return before_zeroing_dtz(wdlScore);

    int dtzScore = probe_table<DTZ>(pos, ps, wdlScore);

    if (*ps == PS_FAIL)
        return 0;

    if (*ps != PS_AC_CHANGED)
        return (dtzScore + 100 * (wdlScore == WDL_BLESSED_LOSS || wdlScore == WDL_CURSED_WIN))
             * sign(wdlScore);

    // DTZ-score stores results for the other side, so need to do a 1-ply search
    // and find the winning move that minimizes DTZ-score.
    int minDtzScore = 0xFFFF;

    for (auto m : MoveList<LEGAL>(pos))
    {
        bool zeroing = pos.capture(m) || type_of(pos.moved_piece(m)) == PAWN;

        State st;
        pos.do_move(m, st);

        // For zeroing moves want the dtzScore of the move _before_ doing it,
        // otherwise will get the dtzScore of the next move sequence.
        // Search the position after the move to get the WDL-score sign
        // (because even in a winning position could make a losing capture or go for a draw).
        dtzScore = zeroing ? -before_zeroing_dtz(search<false>(pos, ps)) : -probe_dtz(pos, ps);

        // If the move mates, force min DTZ-score to 1
        if (dtzScore == 1 && pos.checkers() && MoveList<LEGAL, true>(pos).empty())
            minDtzScore = 1;

        // Convert result from 1-ply search. Zeroing moves are already accounted
        // by dtz_before_zeroing() that returns the DTZ of the previous move.
        if (!zeroing)
            dtzScore += sign(dtzScore);

        // Skip the draws and if winning only pick positive DTZ-score
        if (sign(dtzScore) == sign(wdlScore))
            if (minDtzScore > dtzScore)
                minDtzScore = dtzScore;

        pos.undo_move(m);

        if (*ps == PS_FAIL)
            return 0;
    }

    // When there are no legal moves, the position is mate: return -1
    return minDtzScore == 0xFFFF ? -1 : minDtzScore;
}

// clang-format off

// Use the DTZ-tables to rank root moves.
//
// A return value false indicates that not all probes were successful.
bool probe_root_dtz(Position& pos, RootMoves& rootMoves, bool rule50Active, bool rankDTZ, std::function<bool()> time_to_abort) noexcept {
    // Obtain 50-move counter for the root position
    std::int16_t rule50Count = pos.rule50_count();

    // Check whether the position was repeated since the last zeroing move
    bool rep = pos.has_repeated();

    int bound = rule50Active ? (MAX_DTZ / 2 - 100) : 1;

    // Probe and rank each move
    for (auto& rm : rootMoves)
    {
        State st;
        pos.do_move(rm.pv[0], st);

        ProbeState ps = PS_OK;
        int        dtzScore;
        // Calculate dtzScore for the current move counting from the root position
        if (pos.rule50_count() == 0)
        {
            // In case of a zeroing move, dtzScore is one of -101/-1/0/1/101
            dtzScore = before_zeroing_dtz(-probe_wdl(pos, &ps));
        }
        else if (pos.is_draw(1, rule50Active))
        {
            // In case a root move leads to a draw by repetition or 50-move rule,
            // set dtzScore to zero. Note: since are only 1 ply from the root,
            // this must be a true 3-fold repetition inside the game history.
            dtzScore = 0;
        }
        else
        {
            // Otherwise, take dtzScore for the new position and correct by 1 ply
            dtzScore = -probe_dtz(pos, &ps);
            dtzScore = dtzScore > 0 ? dtzScore + 1 : dtzScore < 0 ? dtzScore - 1 : dtzScore;
        }

        // Make sure that a mating move is assigned a dtzScore value of 1
        if (dtzScore == 2 && pos.checkers() && MoveList<LEGAL, true>(pos).empty())
            dtzScore = 1;

        pos.undo_move(rm.pv[0]);

        if (ps == PS_FAIL)
            return false;
        if (time_to_abort())
        {
            //UCI::print_info_string("Unable to completely probe Syzygy DTZ tables due to time pressure.");
            return false;
        }

        // Better moves are ranked higher. Certain wins are ranked equally.
        // Losing moves are ranked equally unless a 50-move draw is in sight.
        int r = dtzScore > 0 ? (+1 * dtzScore + rule50Count < 100 && !rep  //
                                  ? +MAX_DTZ - (rankDTZ ? dtzScore : 0)
                                  : +MAX_DTZ / 2 - (+dtzScore + rule50Count))
              : dtzScore < 0 ? (-2 * dtzScore + rule50Count < 100  //
                                  ? -MAX_DTZ - (rankDTZ ? dtzScore : 0)
                                  : -MAX_DTZ / 2 + (-dtzScore + rule50Count))
                             : 0;

        rm.tbRank = r;

        // Determine the WDL-score to be displayed for this move.
        // Assign at least 1 cp to cursed wins and let it grow to 49 cp
        // as the positions gets closer to a real win.
        rm.tbValue = r >= bound ? VALUE_MATES_IN_MAX_PLY - 1
                   : r > 0      ? Value((std::max(r - (+MAX_DTZ / 2 - 200), +3) * VALUE_PAWN) / 200)
                   : r == 0     ? VALUE_DRAW
                   : r > -bound ? Value((std::min(r + (+MAX_DTZ / 2 - 200), -3) * VALUE_PAWN) / 200)
                                : VALUE_MATED_IN_MAX_PLY + 1;
    }

    return true;
}

// Use the WDL-tables to rank root moves.
// This is a fallback for the case that some or all DTZ-tables are missing.
//
// A return value false indicates that not all probes were successful.
bool probe_root_wdl(Position& pos, RootMoves& rootMoves, bool rule50Active) noexcept {
    // Probe and rank each move
    for (auto& rm : rootMoves)
    {
        State st;
        pos.do_move(rm.pv[0], st);

        ProbeState ps       = PS_OK;
        WDLScore   wdlScore = pos.is_draw(1) ? WDL_DRAW : -probe_wdl(pos, &ps);

        pos.undo_move(rm.pv[0]);

        if (ps == PS_FAIL)
            return false;

        rm.tbRank = WDLToRank[wdlScore + 2];

        if (!rule50Active)
            wdlScore = wdlScore > WDL_DRAW ? WDL_WIN : wdlScore < WDL_DRAW ? WDL_LOSS : WDL_DRAW;
        rm.tbValue = WDLToValue[wdlScore + 2];
    }

    return true;
}

Config rank_root_moves(Position& pos, RootMoves& rootMoves, const Options& options, bool rankDTZ, std::function<bool()> time_to_abort) noexcept {
    Config config;

    if (rootMoves.empty())
        return config;

    config.cardinality  = options["SyzygyProbeLimit"];
    config.probeDepth   = options["SyzygyProbeDepth"];
    config.rule50Active = options["Syzygy50MoveRule"];

    bool dtzAvailable = true;

    // Tables with fewer pieces than SyzygyProbeLimit are searched with
    // probeDepth == DEPTH_ZERO
    if (config.cardinality > MaxCardinality)
    {
        config.cardinality = MaxCardinality;
        config.probeDepth  = DEPTH_ZERO;
    }

    if (config.cardinality >= pos.count() && !pos.can_castle(ANY_CASTLING))
    {
        // Rank moves using DTZ-tables, Exit early if the time_to_abort() returns true
        config.rootInTB = probe_root_dtz(pos, rootMoves, config.rule50Active, rankDTZ, time_to_abort);

        if (!config.rootInTB)
        {
            // DTZ-tables are missing/aborted; try to rank moves using WDL-tables
            dtzAvailable    = false;
            config.rootInTB = probe_root_wdl(pos, rootMoves, config.rule50Active);
        }
    }

    if (config.rootInTB)
    {
        // Sort moves according to TB rank
        rootMoves.sort([](const RootMove& rm1, const RootMove& rm2) noexcept {
            return rm1.tbRank > rm2.tbRank;
        });
        // Probe during search only if DTZ is not available and winning
        if (dtzAvailable || rootMoves[0].tbValue <= VALUE_DRAW)
            config.cardinality = 0;
    }
    else
    {
        // Clean up if probe_root_dtz() and probe_root_wdl() have failed
        for (auto& rm : rootMoves)
            rm.tbRank = 0;
    }

    return config;
}

// clang-format on

}  // namespace Tablebases
}  // namespace DON

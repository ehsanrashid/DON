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

#include "tbprobe.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <sys/stat.h>

#include "../bitboard.h"
#include "../misc.h"
#include "../movegen.h"
#include "../position.h"
#include "../search.h"
#include "../uci.h"
#include "../ucioption.h"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #if !defined(NOMINMAX)
        #define NOMINMAX  // Disable macros min() and max()
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
#endif

using namespace DON::Tablebases;

namespace DON {

namespace {

// Max number of supported piece
constexpr std::uint32_t TBPIECES = 7;
// Max DTZ supported (2 times), large enough to deal with the syzygy TB limit.
constexpr int MAX_DTZ = 1 << 18;

enum Endian {
    BigEndian,
    LittleEndian
};
// Used as template parameter
enum TBType {
    WDL,
    DTZ
};

// Each table has a set of flags: all of them refer to DTZ tables, the last one to WDL tables
enum TBFlag {
    AC          = 1,
    Mapped      = 2,
    WinPlies    = 4,
    LossPlies   = 8,
    Wide        = 16,
    SingleValue = 128
};

constexpr Square operator^(Square s, int i) noexcept { return Square(int(s) ^ i); }
constexpr Piece  operator^(Piece pc, int i) noexcept { return Piece(int(pc) ^ i); }

// clang-format off
size_t  PawnsMap[SQUARE_NB];
size_t B1H1H7Map[SQUARE_NB];
size_t A1D1D4Map[SQUARE_NB];
size_t KKMap[10][SQUARE_NB];  // [A1D1D4Map][SQUARE_NB]

size_t     Binomial[6][SQUARE_NB];    // [k][n] k elements from a set of n elements
size_t  LeadPawnIdx[6][SQUARE_NB];    // [leadPawnCnt][SQUARE_NB]
size_t LeadPawnSize[6][FILE_NB / 2];  // [leadPawnCnt][FILE_A..FILE_D]
// clang-format on

// Comparison function to sort leading pawns in ascending PawnsMap[] order
bool pawns_comp(Square s1, Square s2) noexcept { return PawnsMap[s1] < PawnsMap[s2]; }

constexpr int off_A1H8(Square s) noexcept { return int(rank_of(s)) - int(file_of(s)); }

// clang-format off
constexpr std::int32_t WDLToRank [5]{-MAX_DTZ,
                                     -MAX_DTZ + 101,
                                      0,
                                     +MAX_DTZ - 101,
                                     +MAX_DTZ};
constexpr Value        WDLToValue[5]{-VALUE_MATE + MAX_PLY + 1,
                                      VALUE_DRAW - 2,
                                      VALUE_DRAW,
                                      VALUE_DRAW + 2,
                                     +VALUE_MATE - MAX_PLY - 1};
// clang-format on

template<typename T, int Half = sizeof(T) / 2, int End = sizeof(T) - 1>
inline void swap_endian(T& x) noexcept {
    static_assert(std::is_unsigned_v<T>, "Argument of swap_endian not unsigned");

    std::uint8_t tmp, *c = (std::uint8_t*) (&x);
    for (int i = 0; i < Half; ++i)
        tmp = c[i], c[i] = c[End - i], c[End - i] = tmp;
}
template<>
inline void swap_endian<std::uint8_t>(std::uint8_t&) noexcept {}

template<typename T, Endian LE>
T number(void* addr) noexcept {
    T v;

    if (std::uintptr_t(addr) & (alignof(T) - 1))  // Unaligned pointer (very rare)
        std::memcpy(&v, addr, sizeof(T));
    else
        v = *((T*) (addr));

    if (LE != IsLittleEndian)
        swap_endian(v);
    return v;
}

// DTZ tables don't store valid scores for moves that reset the rule50 counter
// like captures and pawn moves but can easily recover the correct dtz of the
// previous move if know the position's WDL score.
int dtz_before_zeroing(WDLScore wdl) noexcept {
    return wdl == WDLWin         ? +1
         : wdl == WDLCursedWin   ? +101
         : wdl == WDLBlessedLoss ? -101
         : wdl == WDLLoss        ? -1
                                 : 0;
}

// Return the sign of a number (-1, 0, +1)
template<typename T>
int sign_of(T val) noexcept {
    return (T(0) < val) - (val < T(0));
}

// Numbers in little-endian used by sparseIndex[] to point into blockLength[]
struct SparseEntry final {
    char block[4];   // Number of block
    char offset[2];  // Offset within the block
};

static_assert(sizeof(SparseEntry) == 6, "SparseEntry must be 6 bytes");

using Sym = std::uint16_t;  // Huffman symbol

struct LR final {
    enum Side : std::uint8_t {
        Left,
        Right
    };

    std::uint8_t lr[3];  // The first 12 bits is the left-hand symbol, the second 12
                         // bits is the right-hand symbol. If the symbol has length 1,
                         // then the left-hand symbol is the stored value.
    template<Side S>
    Sym get() const noexcept {
        return S == Left  ? ((lr[1] & 0xF) << 8) | lr[0]
             : S == Right ? (lr[2] << 4) | (lr[1] >> 4)
                          : (assert(false), Sym(-1));
    }
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

    std::string filename;

   public:
    // Look for and open the file among the Paths directories where the .rtbw
    // and .rtbz files can be found. Multiple directories are separated by ";"
    // on Windows and by ":" on Unix-based operating systems.
    //
    // Example:
    // C:\tb\wdl345;C:\tb\wdl6;D:\tb\dtz345;D:\tb\dtz6
    static std::string Paths;

    explicit TBFile(const std::string& file) noexcept {

#if !defined(_WIN32)
        constexpr char PathSeparator = ':';
#else
        constexpr char PathSeparator = ';';
#endif
        std::istringstream iss(Paths);

        std::string path;
        while (std::getline(iss, path, PathSeparator))
        {
            filename = path + "/" + file;
            std::ifstream::open(filename);
            if (is_open())
                return;
        }
    }

    // Memory map the file and check it.
    std::uint8_t* map(void** baseAddress, std::uint64_t* mapping, TBType type) noexcept {
        if (is_open())
            close();  // Need to re-open to get native file descriptor

#if !defined(_WIN32)
        int fd = ::open(filename.c_str(), O_RDONLY);

        if (fd == -1)
            return *baseAddress = nullptr, nullptr;

        struct stat bufStat;
        fstat(fd, &bufStat);

        if (bufStat.st_size % 64 != 16)
        {
            std::cerr << "Corrupt tablebase file " << filename << '\n';
            std::exit(EXIT_FAILURE);
        }

        *mapping     = bufStat.st_size;
        *baseAddress = mmap(nullptr, bufStat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    #if defined(MADV_RANDOM)
        madvise(*baseAddress, bufStat.st_size, MADV_RANDOM);
    #endif
        ::close(fd);

        if (*baseAddress == MAP_FAILED)
        {
            std::cerr << "Could not mmap(), name = " << filename << '\n';
            std::exit(EXIT_FAILURE);
        }
#else
        // Note FILE_FLAG_RANDOM_ACCESS is only a hint to Windows and as such may get ignored.
        HANDLE fd = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);

        if (fd == INVALID_HANDLE_VALUE)
            return *baseAddress = nullptr, nullptr;

        DWORD highSize;
        DWORD lowSize = GetFileSize(fd, &highSize);

        if (lowSize % 64 != 16)
        {
            std::cerr << "Corrupt tablebase file " << filename << '\n';
            std::exit(EXIT_FAILURE);
        }

        HANDLE mmap = CreateFileMapping(fd, nullptr, PAGE_READONLY, highSize, lowSize, nullptr);
        CloseHandle(fd);

        if (!mmap)
        {
            std::cerr << "CreateFileMapping() failed, name = " << filename << '\n';
            std::exit(EXIT_FAILURE);
        }

        *mapping     = std::uint64_t(mmap);
        *baseAddress = MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, 0);

        if (!*baseAddress)
        {
            std::cerr << "MapViewOfFile() failed, name = " << filename
                      << ", error = " << GetLastError() << '\n';
            std::exit(EXIT_FAILURE);
        }
#endif
        auto data = (std::uint8_t*) (*baseAddress);

        constexpr std::size_t  MAGIC_SIZE = 4;
        constexpr std::uint8_t MAGIC[2][MAGIC_SIZE]{{0xD7, 0x66, 0x0C, 0xA5},
                                                    {0x71, 0xE8, 0x23, 0x5D}};

        if (std::memcmp(data, MAGIC[type == WDL], MAGIC_SIZE))
        {
            std::cerr << "Corrupted table in file " << filename << '\n';
            unmap(*baseAddress, *mapping);
            return *baseAddress = nullptr, nullptr;
        }

        return data + MAGIC_SIZE;  // Skip Magics's header
    }

    static void unmap(void* baseAddress, std::uint64_t mapping) noexcept {

#if !defined(_WIN32)
        munmap(baseAddress, mapping);
#else
        UnmapViewOfFile(baseAddress);
        CloseHandle((HANDLE) mapping);
#endif
    }
};

std::string TBFile::Paths;

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
          symLen;            // Number of values (-1) represented by a given Huffman symbol: 1..256
    Piece pieces[TBPIECES];  // Position pieces: the order of pieces defines the groups
    std::uint64_t
                 groupIdx[TBPIECES + 1];  // Start index used for the encoding of the group's pieces
    std::int32_t groupLen[TBPIECES + 1];  // Number of pieces in a given group: KRKN -> (3, 1)
    std::uint16_t map_idx[4];  // WDLWin, WDLLoss, WDLCursedWin, WDLBlessedLoss (used in DTZ)
};

// struct TBTable contains indexing information to access the corresponding TBFile.
// There are 2 types of TBTable, corresponding to a WDL or a DTZ file. TBTable
// is populated at init time but the nested PairsData records are populated at
// first access, when the corresponding file is memory mapped.
template<TBType Type>
struct TBTable final {
    using Ret = std::conditional_t<Type == WDL, WDLScore, int>;

    static constexpr int SIDES = 1 + 1 * (Type == WDL);

    std::atomic_bool ready;
    void*            baseAddress;
    std::uint8_t*    map;
    std::uint64_t    mapping;
    Key              key[COLOR_NB];
    std::uint8_t     pieceCount;
    bool             hasPawns;
    bool             hasUniquePieces;
    std::uint8_t     pawnCount[COLOR_NB];        // [Lead color / other color]
    PairsData        items[SIDES][FILE_NB / 2];  // [wtm / btm][FILE_A..FILE_D or 0]

    PairsData* get(int ac, int f) noexcept { return &items[ac % SIDES][f * hasPawns]; }

    TBTable() noexcept :
        ready(false),
        baseAddress(nullptr) {}
    explicit TBTable(const std::string& code) noexcept;
    explicit TBTable(const TBTable<WDL>& wdl) noexcept;

    ~TBTable() noexcept {
        if (baseAddress)
            TBFile::unmap(baseAddress, mapping);
    }
};

template<>
TBTable<WDL>::TBTable(const std::string& code) noexcept :
    TBTable() {

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    Position pos;

    pos.set(code, WHITE, &st);
    key[WHITE] = pos.material_key();
    pieceCount = pos.count<ALL_PIECE>();
    hasPawns   = pos.count<PAWN>();

    hasUniquePieces = false;
    for (Color c : {WHITE, BLACK})
        for (PieceType pt = PAWN; pt < KING; ++pt)
            if (pos.count(c, pt) == 1)
                hasUniquePieces = true;

    // Set the leading color. In case both sides have pawns the leading color
    // is the side with fewer pawns because this leads to better compression.
    bool c = !pos.count<PAWN>(BLACK)
          || (pos.count<PAWN>(WHITE) && pos.count<PAWN>(BLACK) >= pos.count<PAWN>(WHITE));

    pawnCount[WHITE] = pos.count<PAWN>(c ? WHITE : BLACK);
    pawnCount[BLACK] = pos.count<PAWN>(c ? BLACK : WHITE);

    pos.set(code, BLACK, &st);
    key[BLACK] = pos.material_key();
}

template<>
TBTable<DTZ>::TBTable(const TBTable<WDL>& wdl) noexcept :
    TBTable() {

    // Use the corresponding WDL table to avoid recalculating all from scratch
    key[WHITE]       = wdl.key[WHITE];
    key[BLACK]       = wdl.key[BLACK];
    pieceCount       = wdl.pieceCount;
    hasPawns         = wdl.hasPawns;
    hasUniquePieces  = wdl.hasUniquePieces;
    pawnCount[WHITE] = wdl.pawnCount[WHITE];
    pawnCount[BLACK] = wdl.pawnCount[BLACK];
}

// class TBTables creates and keeps ownership of the TBTable objects, one for
// each TB file found. It supports a fast, hash-based, table lookup. Populated
// at init time, accessed at probe time.
class TBTables final {

    struct Entry final {
        Key           key;
        TBTable<WDL>* wdl;
        TBTable<DTZ>* dtz;

        template<TBType Type>
        TBTable<Type>* get() const noexcept {
            return static_cast<TBTable<Type>*>(Type == WDL ? (void*) wdl : (void*) dtz);
        }
    };

    static constexpr int SIZE     = 1 << 12;  // 4K table, indexed by key's 12 lsb
    static constexpr int OVERFLOW = 1;  // Number of elements allowed to map to the last bucket

    static constexpr Key16 get_index(Key key) noexcept { return Key16(key & (SIZE - 1)); }

    Entry hashTable[SIZE + OVERFLOW];

    std::deque<TBTable<WDL>> wdlTable;
    std::deque<TBTable<DTZ>> dtzTable;

    std::size_t wdlFileFound = 0;
    std::size_t dtzFileFound = 0;

    void insert(Key key, TBTable<WDL>* wdl, TBTable<DTZ>* dtz) noexcept {
        Entry entry{key, wdl, dtz};

        Key16 homeBucket = get_index(key);
        // Ensure last element is empty to avoid overflow when looking up
        for (Key16 bucket = homeBucket; bucket < SIZE + OVERFLOW - 1; ++bucket)
        {
            Key otherKey = hashTable[bucket].key;
            if (otherKey == key || !hashTable[bucket].get<WDL>())
            {
                hashTable[bucket] = entry;
                return;
            }

            // Robin Hood hashing: If we've probed for longer than this element,
            // insert here and search for a new spot for the other element instead.
            Key16 otherHomeBucket = get_index(otherKey);
            if (otherHomeBucket > homeBucket)
            {
                std::swap(entry, hashTable[bucket]);
                key        = otherKey;
                homeBucket = otherHomeBucket;
            }
        }
        std::cerr << "TB hash table size too low!\n";
        std::exit(EXIT_FAILURE);
    }

   public:
    template<TBType Type>
    TBTable<Type>* get(Key key) noexcept {
        const Entry* entry = &hashTable[get_index(key)];
        while (true)
        {
            if (entry->key == key || !entry->get<Type>())
                return entry->get<Type>();
            ++entry;
        }
    }

    void clear() noexcept {
        std::memset(hashTable, 0, sizeof(hashTable));
        wdlTable.clear();
        dtzTable.clear();
        wdlFileFound = 0;
        dtzFileFound = 0;
    }

    std::size_t wdl_found() const noexcept { return wdlFileFound; }
    std::size_t dtz_found() const noexcept { return dtzFileFound; }

    void add(const std::vector<PieceType>& pieces) noexcept;
};

// If the corresponding file exists two new objects TBTable<WDL> and TBTable<DTZ>
// are created and added to the lists and hash table. Called at init time.
void TBTables::add(const std::vector<PieceType>& pieces) noexcept {

    std::string code;
    for (PieceType pt : pieces)
        code += UCI::piece(pt);
    code.insert(code.find('K', 1), "v");  // KRK -> KRvK

    TBFile dtzFile(code + ".rtbz");
    if (dtzFile.is_open())
    {
        dtzFile.close();
        dtzFileFound++;
    }

    TBFile wdlFile(code + ".rtbw");
    if (!wdlFile.is_open())  // Only WDL file is checked
        return;

    wdlFile.close();
    wdlFileFound++;

    MaxCardinality = std::max<std::uint8_t>(MaxCardinality, pieces.size());

    wdlTable.emplace_back(code);
    dtzTable.emplace_back(wdlTable.back());

    // Insert into the hash keys for both colors: KRvK with KR white and black
    insert(wdlTable.back().key[WHITE], &wdlTable.back(), &dtzTable.back());
    insert(wdlTable.back().key[BLACK], &wdlTable.back(), &dtzTable.back());
}

TBTables TBTables;

// TB tables are compressed with canonical Huffman code. The compressed data is divided into
// blocks of size d->blockSize, and each block stores a variable number of symbols.
// Each symbol represents either a WDL or a (remapped) DTZ value, or a pair of other symbols
// (recursively). If you keep expanding the symbols in a block, you end up with up to 65536
// WDL or DTZ values. Each symbol represents up to 256 values and will correspond after
// Huffman coding to at least 1 bit. So a block of 32 bytes corresponds to at most
// 32 x 8 x 256 = 65536 values. This maximum is only reached for tables that consist mostly
// of draws or mostly of wins, but such tables are actually quite common. In principle, the
// blocks in WDL tables are 64 bytes long (and will be aligned on cache lines). But for
// mostly-draw or mostly-win tables this can leave many 64-byte blocks only half-filled, so
// in such cases blocks are 32 bytes long. The blocks of DTZ tables are up to 1024 bytes long.
// The generator picks the size that leads to the smallest table. The "book" of symbols and
// Huffman codes are the same for all blocks in the table. A non-symmetric pawnless TB file
// will have one table for wtm and one for btm, a TB file with pawns will have tables per
// file a,b,c,d also, in this case, one set for wtm and one for btm.
int decompress_pairs(PairsData* d, std::uint64_t idx) noexcept {

    // Special case where all table positions store the same value
    if (d->flags & SingleValue)
        return d->minSymLen;

    // First need to locate the right block that stores the value at index "idx".
    // Because each block n stores blockLength[n] + 1 values, the index i of the block
    // that contains the value at position idx is:
    //
    //                    for (i = -1, sum = 0; sum <= idx; ++i)
    //                        sum += blockLength[i + 1] + 1;
    //
    // This can be slow, so use SparseIndex[] populated with a set of SparseEntry that
    // point to known indices into blockLength[]. Namely SparseIndex[k] is a SparseEntry
    // that stores the blockLength[] index and the offset within that block of the value
    // with index I(k), where:
    //
    //       I(k) = k * d->span + d->span / 2      (1)

    // First step is to get the 'k' of the I(k) nearest to our idx, using definition (1)
    auto k = std::uint32_t(idx / d->span);

    // Then read the corresponding SparseIndex[] entry
    auto block  = number<std::uint32_t, LittleEndian>(&d->sparseIndex[k].block);
    int  offset = number<std::uint16_t, LittleEndian>(&d->sparseIndex[k].offset);

    // Now compute the difference idx - I(k). From the definition of k,
    //
    //       idx = k * d->span + idx % d->span    (2)
    //
    // So from (1) and (2) can compute idx - I(K):
    int diff = idx % d->span - d->span / 2;

    // Sum the above to offset to find the offset corresponding to our idx
    offset += diff;

    // Move to the previous/next block, until reach the correct block that contains idx,
    // that is when 0 <= offset <= d->blockLength[block]
    while (offset < 0)
        offset += d->blockLength[--block] + 1;

    while (offset > d->blockLength[block])
        offset -= d->blockLength[block++] + 1;

    // Finally, find the start address of our block of canonical Huffman symbols
    auto* ptr = (std::uint32_t*) (d->data + (std::uint64_t(block) * d->blockSize));

    // Read the first 64 bits in our block, this is a (truncated) sequence of
    // unknown number of symbols of unknown length but the first one
    // is at the beginning of this 64-bit sequence.
    auto buf64     = number<std::uint64_t, BigEndian>(ptr);
    int  buf64Size = 64;
    ptr += 2;
    Sym sym;

    while (true)
    {
        size_t len = 0;  // This is the symbol length - d->min_sym_len

        // Now get the symbol length.
        // For any symbol s64 of length 'l' right-padded to 64 bits that
        // d->base64[l-1] >= s64 >= d->base64[l]
        // so can find the symbol length iterating through base64[].
        while (buf64 < d->base64[len])
            ++len;

        // All the symbols of a given length are consecutive integers (numerical sequence property),
        // so can compute the offset of our symbol of length len, stored at the beginning of buf64.
        sym = Sym((buf64 - d->base64[len]) >> (64 - len - d->minSymLen));

        // Now add the value of the lowest symbol of length len to get our symbol
        sym += number<Sym, LittleEndian>(&d->lowestSym[len]);

        // If our offset is within the number of values represented by symbol 'sym', are done.
        if (offset < d->symLen[sym] + 1)
            break;

        // ...otherwise update the offset and continue to iterate
        offset -= d->symLen[sym] + 1;
        len += d->minSymLen;  // Get the real length
        buf64 <<= len;        // Consume the just processed symbol
        buf64Size -= len;

        // Refill the buffer
        if (buf64Size <= 32)
        {
            buf64Size += 32;
            buf64 |= std::uint64_t(number<std::uint32_t, BigEndian>(ptr++)) << (64 - buf64Size);
        }
    }

    // Now have our symbol that expands into d->symLen[sym] + 1 symbols.
    // Binary-search for our value recursively expanding into the left and
    // right child symbols until reach a leaf node where symLen[sym] + 1 == 1
    // that will store the value need.
    while (d->symLen[sym])
    {
        Sym lSym = d->btree[sym].get<LR::Left>();

        // If a symbol contains 36 sub-symbols (d->symLen[sym] + 1 = 36) and
        // expands in a pair (d->symLen[lSym] = 23, d->symLen[rSym] = 11), then
        // for instance, the tenth value (offset = 10) will be on the left side
        // because in Recursive Pairing child symbols are adjacent.
        if (offset < d->symLen[lSym] + 1)
            sym = lSym;
        else
        {
            offset -= d->symLen[lSym] + 1;
            sym = d->btree[sym].get<LR::Right>();
        }
    }

    return d->btree[sym].get<LR::Left>();
}

bool check_dtz_ac(TBTable<WDL>*, int, File) noexcept { return true; }

bool check_dtz_ac(TBTable<DTZ>* entry, int ac, File f) noexcept {

    auto flags = entry->get(ac, f)->flags;
    return (flags & AC) == ac || (!entry->hasPawns && entry->key[WHITE] == entry->key[BLACK]);
}

// DTZ scores are sorted by frequency of occurrence and then assigned the
// values 0, 1, 2, ... in order of decreasing frequency. This is done for each
// of the four WDLScore values. The mapping information necessary to reconstruct
// the original values are stored in the TB file and read during map[] init.
WDLScore map_score(TBTable<WDL>*, File, int value, WDLScore) noexcept {
    return WDLScore(value - 2);
}

int map_score(TBTable<DTZ>* entry, File f, int value, WDLScore wdl) noexcept {

    constexpr int WDLMap[5]{1, 3, 0, 2, 0};

    std::uint8_t flags = entry->get(0, f)->flags;

    std::uint8_t*  map = entry->map;
    std::uint16_t* idx = entry->get(0, f)->map_idx;
    if (flags & Mapped)
        value = (flags & Wide) ? ((std::uint16_t*) map)[idx[WDLMap[wdl + 2]] + value]
                               : map[idx[WDLMap[wdl + 2]] + value];

    // DTZ tables store distance to zero in number of moves or plies. We
    // want to return plies, so have to convert to plies when needed.
    if ((wdl == WDLWin && !(flags & WinPlies)) || (wdl == WDLLoss && !(flags & LossPlies))
        || wdl == WDLCursedWin || wdl == WDLBlessedLoss)
        value *= 2;

    return value + 1;
}

// A temporary fix for the compiler bug with AVX-512. (#4450)
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
CLANG_AVX512_BUG_FIX Ret
do_probe_table(const Position& pos, T* entry, WDLScore wdl, ProbeState* result) noexcept {

    Square        squares[TBPIECES];
    Piece         pieces[TBPIECES];
    std::uint64_t idx;
    Bitboard      b, leadPawns = 0;
    int           size = 0, leadPawnCnt = 0;
    File          tbFile = FILE_A;

    // A given TB entry like KRK has associated two material keys: KRvk and Kvkr.
    // If both sides have the same pieces keys are equal. In this case TB tables
    // only stores the 'white to move' case, so if the position to lookup has black
    // to move, need to switch the color and flip the squares before to lookup.
    bool symmetricBlackToMove =
      entry->key[WHITE] == entry->key[BLACK] && pos.active_color() == BLACK;

    // TB files are calculated for white as the stronger side. For instance, we
    // have KRvK, not KvKR. A position where the stronger side is white will have
    // its material key == entry->key[WHITE], otherwise have to switch the color
    // and flip the squares before to lookup.
    bool blackStronger = pos.material_key() != entry->key[WHITE];

    int colorFlip  = (symmetricBlackToMove || blackStronger) * 8;
    int squareFlip = (symmetricBlackToMove || blackStronger) * 56;
    int ac         = (symmetricBlackToMove || blackStronger) ^ pos.active_color();

    // For pawns, TB files store 4 separate tables according if leading pawn is on
    // file a, b, c or d after reordering. The leading pawn is the one with maximum
    // PawnsMap[] value, that is the one most toward the edges and with lowest rank.
    if (entry->hasPawns)
    {
        // In all the 4 tables, pawns are at the beginning of the piece sequence and
        // their color is the reference one. So just pick the first one.
        auto pc = Piece(entry->get(0, 0)->pieces[0] ^ colorFlip);
        assert(type_of(pc) == PAWN);

        leadPawns = b = pos.pieces(color_of(pc), PAWN);
        do
            squares[size++] = pop_lsb(b) ^ squareFlip;
        while (b);

        leadPawnCnt = size;

        std::swap(squares[0], *std::max_element(squares, squares + leadPawnCnt, pawns_comp));

        tbFile = edge_distance(file_of(squares[0]));
    }

    // DTZ tables are one-sided, i.e. they store positions only for white to
    // move or only for black to move, so check for side to move to be ac,
    // early exit otherwise.
    if (!check_dtz_ac(entry, ac, tbFile))
        return *result = CHANGE_AC, Ret();

    // Now ready to get all the position pieces (but the lead pawns)
    // and directly map them to the correct square and color.
    b = pos.pieces() ^ leadPawns;
    while (b)
    {
        Square s       = pop_lsb(b);
        squares[size]  = s ^ squareFlip;
        pieces[size++] = pos.piece_on(s) ^ colorFlip;
    }

    assert(size >= 2);

    PairsData* data = entry->get(ac, tbFile);

    // Then reorder the pieces to have the same sequence as the one stored
    // in pieces[i]: the sequence that ensures the best compression.
    for (int i = leadPawnCnt; i < size - 1; ++i)
        for (int j = i + 1; j < size; ++j)
            if (data->pieces[i] == pieces[j])
            {
                std::swap(pieces[i], pieces[j]);
                std::swap(squares[i], squares[j]);
                break;
            }

    // Now map again the squares so that the square of the lead piece is in
    // the triangle A1-D1-D4.
    if (file_of(squares[0]) > FILE_D)
        for (int i = 0; i < size; ++i)
            squares[i] = flip_file(squares[i]);

    // Encode leading pawns starting with the one with minimum PawnsMap[] and
    // proceeding in ascending order.
    if (entry->hasPawns)
    {
        idx = LeadPawnIdx[leadPawnCnt][squares[0]];

        std::stable_sort(squares + 1, squares + leadPawnCnt, pawns_comp);

        for (int i = 1; i < leadPawnCnt; ++i)
            idx += Binomial[i][PawnsMap[squares[i]]];

        goto ENCODE_END;  // With pawns have finished special treatments
    }

    // In positions without pawns, further flip the squares to ensure leading
    // piece is below RANK_5.
    if (rank_of(squares[0]) > RANK_4)
        for (int i = 0; i < size; ++i)
            squares[i] = flip_rank(squares[i]);

    // Look for the first piece of the leading group not on the A1-D4 diagonal
    // and ensure it is mapped below the diagonal.
    for (std::int32_t i = 0; i < data->groupLen[0]; ++i)
    {
        if (!off_A1H8(squares[i]))
            continue;

        if (off_A1H8(squares[i]) > 0)  // A1-H8 diagonal flip: SQ_A3 -> SQ_C1
            for (int j = i; j < size; ++j)
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
            idx =
              (A1D1D4Map[squares[0]] * 63 + (squares[1] - adjust1)) * 62 + (squares[2] - adjust2);

        // First piece is on a1-h8 diagonal, second below: map this occurrence to
        // 6 to differentiate from the above case, rank_of() maps a1-d4 diagonal
        // to 0...3 and finally B1H1H7Map[] maps the b1-h1-h7 triangle to 0..27.
        else if (off_A1H8(squares[1]))
            idx = (6 * 63 + rank_of(squares[0]) * 28 + B1H1H7Map[squares[1]]) * 62
                + (squares[2] - adjust2);

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
    idx *= data->groupIdx[0];
    Square* groupSq = squares + data->groupLen[0];

    // Encode remaining pawns and then pieces according to square, in ascending order
    bool pawnsRemaining = entry->hasPawns && entry->pawnCount[1];

    int next = 0;
    while (data->groupLen[++next])
    {
        std::stable_sort(groupSq, groupSq + data->groupLen[next]);
        std::uint64_t n = 0;

        // Map down a square if "comes later" than a square in the previous
        // groups (similar to what was done earlier for leading group pieces).
        for (std::int32_t i = 0; i < data->groupLen[next]; ++i)
        {
            auto adjust =
              std::count_if(squares, groupSq, [&](Square s) -> bool { return groupSq[i] > s; });
            n += Binomial[i + 1][int(groupSq[i]) - adjust - 8 * pawnsRemaining];
        }

        pawnsRemaining = false;
        idx += n * data->groupIdx[next];
        groupSq += data->groupLen[next];
    }

    // Now that have the index, decompress the pair and get the score
    return map_score(entry, tbFile, decompress_pairs(data, idx), wdl);
}

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
void set_groups(T& entry, PairsData* d, size_t order[], File f) noexcept {

    size_t n        = 0;
    int    firstLen = entry.hasPawns ? 0 : entry.hasUniquePieces ? 3 : 2;
    d->groupLen[n]  = 1;

    // Number of pieces per group is stored in groupLen[], for instance in KRKN
    // the encoder will default on '111', so groupLen[] will be (3, 1).
    for (std::uint8_t i = 1; i < entry.pieceCount; ++i)
    {
        if (--firstLen > 0 || d->pieces[i] == d->pieces[i - 1])
            d->groupLen[n]++;
        else
            d->groupLen[++n] = 1;
    }
    d->groupLen[++n] = 0;  // Zero-terminated

    // The sequence in pieces[] defines the groups, but not the order in which
    // they are encoded. If the pieces in a group g can be combined on the board
    // in N(g) different ways, then the position encoding will be of the form:
    //
    //           g1 * N(g2) * N(g3) + g2 * N(g3) + g3
    //
    // This ensures unique encoding for the whole position. The order of the
    // groups is a per-table parameter and could not follow the canonical leading
    // pawns/pieces -> remaining pawns -> remaining pieces. In particular the
    // first group is at order[0] position and the remaining pawns, when present,
    // are at order[1] position.
    bool   pp = entry.hasPawns && entry.pawnCount[1] != 0;  // Pawns on both sides
    size_t i  = pp ? 2 : 1;

    std::size_t   freeLen = 64 - d->groupLen[0] - pp * d->groupLen[1];
    std::uint64_t idx     = 1;

    for (size_t k = 0; k == order[0] || k == order[1] || i < n; ++k)
    {
        // Leading pawns or pieces
        if (k == order[0])
        {
            d->groupIdx[0] = idx;
            idx *= entry.hasPawns        ? LeadPawnSize[d->groupLen[0]][f]
                 : entry.hasUniquePieces ? 31332
                                         : 462;
            continue;
        }
        // Remaining pawns
        if (k == order[1])
        {
            d->groupIdx[1] = idx;
            idx *= Binomial[d->groupLen[1]][48 - d->groupLen[0]];
            continue;
        }
        // Remaining pieces
        d->groupIdx[i] = idx;
        idx *= Binomial[d->groupLen[i]][freeLen];
        freeLen -= d->groupLen[i];
        ++i;
    }
    d->groupIdx[n] = idx;
}

// In Recursive Pairing each symbol represents a pair of children symbols. So
// read d->btree[] symbols data and expand each one in his left and right child
// symbol until reaching the leaves that represent the symbol value.
std::uint8_t set_symlen(PairsData* d, Sym s, std::vector<bool>& visited) noexcept {

    visited[s] = true;  // Can set it now because tree is acyclic

    Sym rSym = d->btree[s].get<LR::Right>();

    if (rSym == 0xFFF)
        return 0;

    Sym lSym = d->btree[s].get<LR::Left>();

    if (!visited[lSym])
        d->symLen[lSym] = set_symlen(d, lSym, visited);

    if (!visited[rSym])
        d->symLen[rSym] = set_symlen(d, rSym, visited);

    return d->symLen[lSym] + d->symLen[rSym] + 1;
}

std::uint8_t* set_sizes(PairsData* d, std::uint8_t* data) noexcept {

    d->flags = *data++;

    if (d->flags & SingleValue)
    {
        d->blockCount      = 0;
        d->blockLengthSize = 0;
        d->span            = 0;
        d->sparseIndexSize = 0;        // Broken MSVC zero-init
        d->minSymLen       = *data++;  // Here store the single value
        return data;
    }

    // groupLen[] is a zero-terminated list of group lengths, the last groupIdx[]
    // element stores the biggest index that is the tb size.
    std::uint64_t tbSize = d->groupIdx[std::find(d->groupLen, d->groupLen + 7, 0) - d->groupLen];

    d->blockSize       = 1u << *data++;
    d->span            = 1u << *data++;
    d->sparseIndexSize = std::size_t((tbSize + d->span - 1) / d->span);  // Round up

    auto padding  = number<std::uint8_t, LittleEndian>(data++);
    d->blockCount = number<std::uint32_t, LittleEndian>(data);
    data += sizeof(std::uint32_t);
    d->blockLengthSize =
      d->blockCount + padding;  // Padded to ensure SparseIndex[] does not point out of range.
    d->maxSymLen = *data++;
    d->minSymLen = *data++;
    d->lowestSym = (Sym*) (data);
    d->base64.resize(d->maxSymLen - d->minSymLen + 1);

    // See https://en.wikipedia.org/wiki/Huffman_coding
    // The canonical code is ordered such that longer symbols (in terms of
    // the number of bits of their Huffman code) have a lower numeric value,
    // so that d->lowestSym[i] >= d->lowestSym[i+1] (when read as LittleEndian).
    // Starting from this compute a base64[] table indexed by symbol length
    // and containing 64 bit values so that d->base64[i] >= d->base64[i+1].

    // Implementation note: first cast the unsigned size_t "base64.size()"
    // to a signed int "base64_size" variable and then are able to subtract 2,
    // avoiding unsigned overflow warnings.

    std::size_t base64Size = d->base64.size();
    for (std::size_t i = base64Size - 1; i > 0; --i)
    {
        d->base64[i - 1] = (d->base64[i]                                       //
                            + number<Sym, LittleEndian>(&d->lowestSym[i - 1])  //
                            - number<Sym, LittleEndian>(&d->lowestSym[i]))
                         / 2;

        assert(2 * d->base64[i - 1] >= d->base64[i]);
    }

    // Now left-shift by an amount so that d->base64[i] gets shifted 1 bit more
    // than d->base64[i+1] and given the above assert condition, ensure that
    // d->base64[i] >= d->base64[i+1]. Moreover for any symbol s64 of length i
    // and right-padded to 64 bits holds d->base64[i-1] >= s64 >= d->base64[i].
    for (std::size_t i = 0; i < base64Size; ++i)
        d->base64[i] <<= 64 - i - d->minSymLen;  // Right-padding to 64 bits

    data += base64Size * sizeof(Sym);
    d->symLen.resize(number<std::uint16_t, LittleEndian>(data));
    data += sizeof(std::uint16_t);
    d->btree = (LR*) (data);

    // The compression scheme used is "Recursive Pairing", that replaces the most
    // frequent adjacent pair of symbols in the source message by a new symbol,
    // reevaluating the frequencies of all of the symbol pairs with respect to
    // the extended alphabet, and then repeating the process.
    // See https://web.archive.org/web/20201106232444/http://www.larsson.dogma.net/dcc99.pdf
    std::vector<bool> visited(d->symLen.size());
    for (std::size_t sym = 0; sym < d->symLen.size(); ++sym)
        if (!visited[sym])
            d->symLen[sym] = set_symlen(d, sym, visited);

    return data + d->symLen.size() * sizeof(LR) + (d->symLen.size() & 1);
}

std::uint8_t* set_dtz_map(TBTable<WDL>&, std::uint8_t* data, File) noexcept { return data; }

std::uint8_t* set_dtz_map(TBTable<DTZ>& entry, std::uint8_t* data, File maxFile) noexcept {

    entry.map = data;

    for (File f = FILE_A; f <= maxFile; ++f)
    {
        std::uint8_t flags = entry.get(0, f)->flags;
        if (flags & Mapped)
        {
            if (flags & Wide)
            {
                data += std::uintptr_t(data) & 1;  // Word alignment, may have a mixed table
                for (auto& idx : entry.get(0, f)->map_idx)
                {
                    // Sequence like 3,x,x,x,1,x,0,2,x,x
                    idx = (std::uint16_t*) (data) - (std::uint16_t*) (entry.map) + 1;
                    data += 2 * number<std::uint16_t, LittleEndian>(data) + 2;
                }
            }
            else
            {
                for (auto& idx : entry.get(0, f)->map_idx)
                {
                    idx = data - entry.map + 1;
                    data += *data + 1;
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

    PairsData* d;

    enum : std::uint8_t {
        Split    = 1,
        HasPawns = 2
    };

    assert(entry.hasPawns == bool(*data & HasPawns));
    assert((entry.key[WHITE] != entry.key[BLACK]) == bool(*data & Split));

    ++data;  // First byte stores flags

    const size_t sides   = T::SIDES == 2 && entry.key[WHITE] != entry.key[BLACK] ? 2 : 1;
    const File   maxFile = entry.hasPawns ? FILE_D : FILE_A;

    bool pp = entry.hasPawns && entry.pawnCount[BLACK];  // Pawns on both sides

    assert(!pp || entry.pawnCount[WHITE]);

    for (File f = FILE_A; f <= maxFile; ++f)
    {
        for (size_t i = 0; i < sides; ++i)
            *entry.get(i, f) = PairsData();

        size_t order[2][2]{{size_t(*data & 0xF), size_t(pp ? *(data + 1) & 0xF : 0xF)},
                           {size_t(*data >> 04), size_t(pp ? *(data + 1) >> 04 : 0xF)}};
        data += 1 + pp;

        for (size_t i = 0; i < sides; ++i)
            for (std::uint8_t k = 0; k < entry.pieceCount; ++k, ++data)
                entry.get(i, f)->pieces[k] = Piece(i ? *data >> 4 : *data & 0xF);

        for (size_t i = 0; i < sides; ++i)
            set_groups(entry, entry.get(i, f), order[i], f);
    }

    data += std::uintptr_t(data) & 1;  // Word alignment

    for (File f = FILE_A; f <= maxFile; ++f)
        for (size_t i = 0; i < sides; ++i)
            data = set_sizes(entry.get(i, f), data);

    data = set_dtz_map(entry, data, maxFile);

    for (File f = FILE_A; f <= maxFile; ++f)
        for (size_t i = 0; i < sides; ++i)
        {
            (d = entry.get(i, f))->sparseIndex = (SparseEntry*) (data);
            data += d->sparseIndexSize * sizeof(SparseEntry);
        }

    for (File f = FILE_A; f <= maxFile; ++f)
        for (size_t i = 0; i < sides; ++i)
        {
            (d = entry.get(i, f))->blockLength = (std::uint16_t*) (data);
            data += d->blockLengthSize * sizeof(std::uint16_t);
        }

    for (File f = FILE_A; f <= maxFile; ++f)
        for (size_t i = 0; i < sides; ++i)
        {
            data = (std::uint8_t*) ((std::uintptr_t(data) + 0x3F) & ~0x3F);  // 64 byte alignment
            (d = entry.get(i, f))->data = data;
            data += d->blockCount * d->blockSize;
        }
}

// If the TB file corresponding to the given position is already memory-mapped
// then return its base address, otherwise, try to memory map and init it.
// Called at every probe, memory map, and init only at first access.
// Function is thread safe and can be called concurrently.
template<TBType Type>
void* mapped(TBTable<Type>& entry, const Position& pos) noexcept {
    static std::mutex mutex;

    // Use 'acquire' to avoid a thread reading 'ready' == true while
    // another is still working. (compiler reordering may cause this).
    if (entry.ready.load(std::memory_order_acquire))
        return entry.baseAddress;  // Could be nullptr if file does not exist

    std::scoped_lock scopedLock(mutex);

    if (entry.ready.load(std::memory_order_relaxed))  // Recheck under lock
        return entry.baseAddress;

    // Pieces strings in decreasing order for each color, like ("KPP","KR")
    std::string w, b;
    for (PieceType pt = KING; pt >= PAWN; --pt)
    {
        w += std::string(popcount(pos.pieces(WHITE, pt)), UCI::piece(pt));
        b += std::string(popcount(pos.pieces(BLACK, pt)), UCI::piece(pt));
    }

    std::string fname = (pos.material_key() == entry.key[WHITE] ? w + 'v' + b : b + 'v' + w)
                      + (Type == WDL ? ".rtbw" : ".rtbz");

    std::uint8_t* data = TBFile(fname).map(&entry.baseAddress, &entry.mapping, Type);

    if (data)
        set(entry, data);

    entry.ready.store(true, std::memory_order_release);
    return entry.baseAddress;
}

template<TBType Type, typename Ret = typename TBTable<Type>::Ret>
Ret probe_table(const Position& pos, ProbeState* result, WDLScore wdl = WDLDraw) noexcept {

    if (pos.count<ALL_PIECE>() == 2)  // KvK
        return Ret(WDLDraw);

    TBTable<Type>* entry = TBTables.get<Type>(pos.material_key());

    if (!entry || !mapped(*entry, pos))
        return *result = FAIL, Ret();

    return do_probe_table(pos, entry, wdl, result);
}

// For a position where the side to move has a winning capture it is not necessary
// to store a winning value so the generator treats such positions as "don't care"
// and tries to assign to it a value that improves the compression ratio. Similarly,
// if the side to move has a drawing capture, then the position is at least drawn.
// If the position is won, then the TB needs to store a win value. But if the
// position is drawn, the TB may store a loss value if that is better for compression.
// All of this means that during probing, the engine must look at captures and probe
// their results and must probe the position itself. The "best" result of these
// probes is the correct result for the position.
// DTZ tables do not store values when a following move is a zeroing winning move
// (winning capture or winning pawn move). Also, DTZ store wrong values for positions
// where the best move is an ep-move (even if losing). So in all these cases set
// the state to BEST_MOVE_ZEROING.
template<bool CheckZeroingMoves>
WDLScore search(Position& pos, ProbeState* result) noexcept {

    WDLScore value, bestValue = WDLLoss;

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    const LegalMoveList legalMoves(pos);
    std::uint8_t        moveCount = 0;

    for (Move m : legalMoves)
    {
        if (!pos.capture(m) && (!CheckZeroingMoves || type_of(pos.moved_piece(m)) != PAWN))
            continue;

        ++moveCount;

        pos.do_move(m, st);
        value = -search<false>(pos, result);
        pos.undo_move(m);

        if (*result == FAIL)
            return WDLDraw;

        if (bestValue < value)
        {
            bestValue = value;

            if (value >= WDLWin)
            {
                *result = BEST_MOVE_ZEROING;  // Winning DTZ-zeroing move
                return value;
            }
        }
    }

    // In case have already searched all the legal moves don't have to probe
    // the TB because the stored score could be wrong. For instance TB tables
    // do not contain information on position with ep rights, so in this case
    // the result of probe_wdl_table is wrong. Also in case of only capture
    // moves, for instance here 4K3/4q3/6p1/2k5/6p1/8/8/8 w - - 0 7, have to
    // return with BEST_MOVE_ZEROING set.
    bool movesNoMore = (moveCount != 0 && moveCount == legalMoves.size());

    if (movesNoMore)
        value = bestValue;
    else
    {
        value = probe_table<WDL>(pos, result);

        if (*result == FAIL)
            return WDLDraw;
    }

    // DTZ stores a "don't care" value if bestValue is a win
    if (bestValue >= value)
        return *result = (bestValue > WDLDraw || movesNoMore ? BEST_MOVE_ZEROING : OK), bestValue;

    return *result = OK, value;
}

}  // namespace

namespace Tablebases {

std::uint8_t MaxCardinality;

// Called at startup to create the various tables
void init() noexcept {

    size_t code;
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
    for (size_t idx = 0; idx < 10; ++idx)
        for (Square s1 = SQ_A1; s1 <= SQ_D4; ++s1)
            if (A1D1D4Map[s1] == idx && (idx || s1 == SQ_B1))  // SQ_B1 is mapped to 0
            {
                for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
                {
                    if ((attacks_bb<KING>(s1) | s1) & s2)
                        continue;  // Illegal position

                    if (!off_A1H8(s1) && off_A1H8(s2) > 0)
                        continue;  // First on diagonal, second above

                    if (!off_A1H8(s1) && !off_A1H8(s2))
                        bothOnDiagonal.emplace_back(idx, s2);
                    else
                        KKMap[idx][s2] = code++;
                }
            }

    // Legal positions with both kings on a diagonal are encoded as last ones
    for (const auto& [fst, snd] : bothOnDiagonal)
        KKMap[fst][snd] = code++;

    // Binomial[] stores the Binomial Coefficients using Pascal rule. There
    // are Binomial[k][n] ways to choose k elements from a set of n elements.
    Binomial[0][0] = 1;
    for (size_t n = SQ_B1; n <= SQ_H8; ++n)                   // Squares
        for (size_t k = 0; k <= std::min<size_t>(n, 5); ++k)  // Pieces
            Binomial[k][n] =
              (k > 0 ? Binomial[k - 1][n - 1] : 0) + (k < n ? Binomial[k][n - 1] : 0);

    std::memset(LeadPawnIdx, 0, sizeof(LeadPawnIdx));
    std::memset(LeadPawnSize, 0, sizeof(LeadPawnSize));
    // PawnsMap[s] encodes squares a2-h7 to a1-h6 (0..47).
    // This is the number of possible available squares when the leading one
    // is in 's'. Moreover the pawn with highest PawnsMap[] is the leading pawn,
    // the one nearest the edge, and among pawns with the same file, the one with the lowest rank.
    code = SQ_H6;  // Available squares (47) when lead pawn is in a2-h7

    // Init the tables for the encoding of leading pawn group:
    // with 7-men TB can have up to 5 leading pawns (KPPPPPK).
    for (size_t leadPawnCnt = 1; leadPawnCnt <= 5; ++leadPawnCnt)
        for (File f = FILE_A; f <= FILE_D; ++f)
        {
            // Restart the index at every file because TB table is split
            // by file, so can reuse the same index for different files.
            int idx = 0;

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
void init(const std::string& paths) noexcept {

    TBTables.clear();
    MaxCardinality = 0;
    TBFile::Paths  = paths;

    if (is_empty(paths))
        return;

    // Add entries in TB tables if the corresponding ".rtbw" file exists
    for (PieceType p1 = PAWN; p1 < KING; ++p1)
    {
        TBTables.add({KING, p1, KING});

        for (PieceType p2 = PAWN; p2 <= p1; ++p2)
        {
            TBTables.add({KING, p1, p2, KING});
            TBTables.add({KING, p1, KING, p2});

            for (PieceType p3 = PAWN; p3 < KING; ++p3)
                TBTables.add({KING, p1, p2, KING, p3});

            for (PieceType p3 = PAWN; p3 <= p2; ++p3)
            {
                TBTables.add({KING, p1, p2, p3, KING});

                for (PieceType p4 = PAWN; p4 <= p3; ++p4)
                {
                    TBTables.add({KING, p1, p2, p3, p4, KING});

                    for (PieceType p5 = PAWN; p5 <= p4; ++p5)
                        TBTables.add({KING, p1, p2, p3, p4, p5, KING});

                    for (PieceType p5 = PAWN; p5 < KING; ++p5)
                        TBTables.add({KING, p1, p2, p3, p4, KING, p5});
                }

                for (PieceType p4 = PAWN; p4 < KING; ++p4)
                {
                    TBTables.add({KING, p1, p2, p3, KING, p4});

                    for (PieceType p5 = PAWN; p5 <= p4; ++p5)
                        TBTables.add({KING, p1, p2, p3, KING, p4, p5});
                }
            }

            for (PieceType p3 = PAWN; p3 <= p1; ++p3)
                for (PieceType p4 = PAWN; p4 <= (p1 == p3 ? p2 : p3); ++p4)
                    TBTables.add({KING, p1, p2, KING, p3, p4});
        }
    }

    sync_cout << "info string Tablebase: "  //
              << TBTables.wdl_found() << " WDL and " << TBTables.dtz_found() << " DTZ found. "
              << "Tablebase files up to " << int(MaxCardinality) << "-man." << sync_endl;
}

// Probe the WDL table for a particular position.
// If *result != FAIL, the probe was successful.
// The return value is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
WDLScore probe_wdl(Position& pos, ProbeState* result) noexcept {

    *result = OK;
    return search<false>(pos, result);
}

// Probe the DTZ table for a particular position.
// If *result != FAIL, the probe was successful.
// The return value is from the point of view of the side to move:
//         n < -100 : loss, but draw under 50-move rule
// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
//        -1        : loss, the side to move is mated
//         0        : draw
//     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
//   100 < n        : win, but draw under 50-move rule
//
// The return value n can be off by 1: a return value -n can mean a loss
// in n+1 ply and a return value +n can mean a win in n+1 ply. This
// cannot happen for tables with positions exactly on the "edge" of
// the 50-move rule.
//
// This implies that if dtz > 0 is returned, the position is certainly
// a win if dtz + 50-move-counter < 100. Care must be taken that the engine
// picks moves that preserve dtz + 50-move-counter < 100.
//
// If n = 100 immediately after a capture or pawn move, then the position
// is also certainly a win, and during the whole phase until the next
// capture or pawn move, the inequality to be preserved is
// dtz + 50-move-counter <= 100.
//
// In short, if a move is available resulting in dtz + 50-move-counter < 100,
// then do not accept moves leading to dtz + 50-move-counter == 100.
int probe_dtz(Position& pos, ProbeState* result) noexcept {

    *result      = OK;
    WDLScore wdl = search<true>(pos, result);

    if (*result == FAIL || wdl == WDLDraw)  // DTZ tables don't store draws
        return 0;

    // DTZ stores a 'don't care value in this case, or even a plain wrong
    // one as in case the best move is a losing ep, so it cannot be probed.
    if (*result == BEST_MOVE_ZEROING)
        return dtz_before_zeroing(wdl);

    int dtz = probe_table<DTZ>(pos, result, wdl);

    if (*result == FAIL)
        return 0;

    if (*result != CHANGE_AC)
        return (dtz + 100 * (wdl == WDLBlessedLoss || wdl == WDLCursedWin)) * sign_of(wdl);

    // DTZ stores results for the other side, so need to do a 1-ply search and
    // find the winning move that minimizes DTZ.
    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    int minDTZ = INT_MAX;

    for (Move m : LegalMoveList(pos))
    {
        bool zeroing = pos.capture(m) || type_of(pos.moved_piece(m)) == PAWN;

        pos.do_move(m, st);

        // For zeroing moves want the dtz of the move _before_ doing it,
        // otherwise will get the dtz of the next move sequence. Search the
        // position after the move to get the score sign (because even in a
        // winning position could make a losing capture or go for a draw).
        dtz = zeroing ? -dtz_before_zeroing(search<false>(pos, result)) : -probe_dtz(pos, result);

        // If the move mates, force minDTZ to 1
        if (dtz == 1 && pos.checkers() && LegalMoveList(pos).empty())
            minDTZ = 1;

        // Convert result from 1-ply search. Zeroing moves are already accounted
        // by dtz_before_zeroing() that returns the DTZ of the previous move.
        if (!zeroing)
            dtz += sign_of(dtz);

        // Skip the draws and if winning only pick positive dtz
        if (minDTZ > dtz && sign_of(dtz) == sign_of(wdl))
            minDTZ = dtz;

        pos.undo_move(m);

        if (*result == FAIL)
            return 0;
    }

    // When there are no legal moves, the position is mate: return -1
    return minDTZ == INT_MAX ? -1 : minDTZ;
}

// Use the DTZ tables to rank root moves.
//
// A return value false indicates that not all probes were successful.
bool root_probe(Position& pos, RootMoves& rootMoves, bool useRule50, bool rankDTZ) noexcept {
    ProbeState result = OK;

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    // Obtain 50-move counter for the root position
    int rule50 = pos.rule50_count();

    // Check whether a position was repeated since the last zeroing move.
    bool rep = pos.has_repeated();

    int dtz, bound = useRule50 ? (MAX_DTZ / 2 - 100) : 1;

    // Probe and rank each move
    for (auto& rm : rootMoves)
    {
        pos.do_move(rm[0], st);

        // Calculate dtz for the current move counting from the root position
        if (pos.rule50_count() == 0)
        {
            // In case of a zeroing move, dtz is one of -101/-1/0/1/101
            dtz = dtz_before_zeroing(-probe_wdl(pos, &result));
        }
        else if (pos.is_draw(1))
        {
            // In case a root move leads to a draw by repetition or 50-move rule,
            // set dtz to zero. Note: since are only 1 ply from the root,
            // this must be a true 3-fold repetition inside the game history.
            dtz = 0;
        }
        else
        {
            // Otherwise, take dtz for the new position and correct by 1 ply
            dtz = -probe_dtz(pos, &result);
            dtz = dtz > 0 ? dtz + 1 : dtz < 0 ? dtz - 1 : dtz;
        }

        // Make sure that a mating move is assigned a dtz value of 1
        if (dtz == 2 && pos.checkers() && LegalMoveList(pos).empty())
            dtz = 1;

        pos.undo_move(rm[0]);

        if (result == FAIL)
            return false;

        // Better moves are ranked higher. Certain wins are ranked equally.
        // Losing moves are ranked equally unless a 50-move draw is in sight.
        int r = dtz > 0 ? (+1 * dtz + rule50 < 100 && !rep ? +MAX_DTZ - rankDTZ * dtz
                                                           : +MAX_DTZ / 2 - (+dtz + rule50))
              : dtz < 0 ? (-2 * dtz + rule50 < 100 /*   */ ? -MAX_DTZ - rankDTZ * dtz
                                                           : -MAX_DTZ / 2 + (-dtz + rule50))
                        : 0;

        rm.tbRank = r;

        // Determine the score to be displayed for this move. Assign at least
        // 1 cp to cursed wins and let it grow to 49 cp as the positions gets
        // closer to a real win.
        rm.tbValue = r >= +bound ? +VALUE_MATE - MAX_PLY - 1
                   : r > 0       ? (std::max(r - (+MAX_DTZ / 2 - 200), +3) * VALUE_PAWN) / 200
                   : r == 0      ? VALUE_DRAW
                   : r > -bound  ? (std::min(r + (+MAX_DTZ / 2 - 200), -3) * VALUE_PAWN) / 200
                                 : -VALUE_MATE + MAX_PLY + 1;
    }

    return true;
}

// Use the WDL tables to rank root moves.
// This is a fallback for the case that some or all DTZ tables are missing.
//
// A return value false indicates that not all probes were successful.
bool root_probe_wdl(Position& pos, RootMoves& rootMoves, bool useRule50) noexcept {
    ProbeState result = OK;

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    // Probe and rank each move
    for (auto& rm : rootMoves)
    {
        pos.do_move(rm[0], st);

        WDLScore wdl = pos.is_draw(1) ? WDLDraw : -probe_wdl(pos, &result);

        pos.undo_move(rm[0]);

        if (result == FAIL)
            return false;

        rm.tbRank = WDLToRank[wdl + 2];

        if (!useRule50)
            wdl = wdl > WDLDraw ? WDLWin : wdl < WDLDraw ? WDLLoss : WDLDraw;
        rm.tbValue = WDLToValue[wdl + 2];
    }

    return true;
}

Config rank_root_moves(Position&      pos,
                       RootMoves&     rootMoves,
                       const Options& options,
                       bool           rankDTZ) noexcept {
    Config tbConfig;

    if (rootMoves.empty())
        return tbConfig;

    tbConfig.rootInTB    = false;
    tbConfig.cardinality = options["SyzygyProbeLimit"];
    tbConfig.probeDepth  = options["SyzygyProbeDepth"];
    tbConfig.useRule50   = options["Syzygy50MoveRule"];

    bool dtzAvailable = true;

    // Tables with fewer pieces than SyzygyProbeLimit are searched with
    // probeDepth == DEPTH_ZERO
    if (tbConfig.cardinality > MaxCardinality)
    {
        tbConfig.cardinality = MaxCardinality;
        tbConfig.probeDepth  = DEPTH_ZERO;
    }

    if (tbConfig.cardinality >= pos.count<ALL_PIECE>() && !pos.can_castle(ANY_CASTLING))
    {
        // Rank moves using DTZ tables
        tbConfig.rootInTB = root_probe(pos, rootMoves, tbConfig.useRule50, rankDTZ);

        if (!tbConfig.rootInTB)
        {
            // DTZ tables are missing; try to rank moves using WDL tables
            dtzAvailable      = false;
            tbConfig.rootInTB = root_probe_wdl(pos, rootMoves, tbConfig.useRule50);
        }
    }

    if (tbConfig.rootInTB)
    {
        // Sort moves according to TB rank
        rootMoves.sort([](const RootMove& rm1, const RootMove& rm2) noexcept -> bool {
            return rm1.tbRank > rm2.tbRank;
        });
        // Probe during search only if DTZ is not available and winning
        if (dtzAvailable || rootMoves.front().tbValue <= VALUE_DRAW)
            tbConfig.cardinality = 0;
    }
    else
        // Clean up if root_probe() and root_probe_wdl() have failed
        for (auto& rm : rootMoves)
            rm.tbRank = 0;

    return tbConfig;
}
}  // namespace Tablebases
}  // namespace DON
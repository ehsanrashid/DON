#include "SyzygyTB.h"

#include <cstdint>
#include <cstdlib>
#include <cstring> // For std::memset and std::memcpy
#include <algorithm>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "BitBoard.h"
#include "Helper.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "Position.h"
#include "Thread.h"
#include "UCI.h"

using std::string;
using std::vector;
using namespace SyzygyTB;

#if defined(_WIN32)

#   if !defined(NOMINMAX)
#       define NOMINMAX // Disable macros min() and max()
#   endif
#   if !defined(WIN32_LEAN_AND_MEAN)
#       define WIN32_LEAN_AND_MEAN // Excludes APIs such as Cryptography, DDE, RPC, Socket
#   endif

#   include <windows.h>

#   undef WIN32_LEAN_AND_MEAN
#   undef NOMINMAX

#else

#   include <fcntl.h>
#   include <unistd.h>
#   include <sys/mman.h>
#   include <sys/stat.h>

#endif

namespace {

    // Type of table
    enum TBType : u08 { WDL, DTZ };

    // Each table has a set of flags: all of them refer to DTZ tables, the last one to WDL tables
    enum TBFlag : u08 {
        STM             = 1 << 0,
        MAPPED          = 1 << 1,
        WIN_PLIES       = 1 << 2,
        LOSS_PLIES      = 1 << 3,
        WIDE            = 1 << 4,
        SINGLE_VALUE    = 1 << 7
    };

    i32 MapPawns[SQUARES];
    i32 MapB1H1H7[SQUARES];
    i32 MapA1D1D4[SQUARES];

    constexpr i16 MapKKSize{ 10 };
    i32 MapKK[MapKKSize][SQUARES]; // [MapA1D1D4][SQUARES]

    i32 Binomial[TBPIECES - 1][SQUARES];      // [k][n] k elements from a set of n elements
    i32 LeadPawnIdx[TBPIECES - 1][SQUARES];   // [lpCount][SQUARES]
    i32 LeadPawnsSize[TBPIECES - 1][FILES/2]; // [lpCount][FILE_A..FILE_D]

    /// Comparison function to sort leading pawns in ascending MapPawns[] order
    bool mapPawnsCompare(Square s1, Square s2) { return MapPawns[s1] < MapPawns[s2]; }
    i32 offA1H8(Square s) { return i32(sRank(s)) - i32(sFile(s)); }

    template<typename T, i16 Half = sizeof (T) / 2, i16 End = sizeof (T) - 1>
    inline void swapEndian(T &x) {
        static_assert (std::is_unsigned<T>::value, "Argument of swapEndian not unsigned");

        u08 *c = (u08*)&x, tmp;
        for (i16 i = 0; i < Half; ++i) {
            tmp = c[i], c[i] = c[End - i], c[End - i] = tmp;
        }
    }
    template<>
    inline void swapEndian<u08>(u08&)
    {}

    template<typename T, bool LE>
    T number(void *addr) {
        static const union { u32 i; char c[4]; } U{ 0x01020304 };
        static bool const LittleEndian = (U.c[0] == 0x04);

        T v;
        if ((uPtr(addr) & (alignof (T) - 1)) != 0) { // Unaligned pointer (very rare)
            memcpy(&v, addr, sizeof (T));
        }
        else {
            v = *((T*)addr);
        }

        if (LE != LittleEndian) {
            swapEndian(v);
        }

        return v;
    }

    // DTZ tables don't store valid scores for moves that reset the move50Rule counter
    // like captures and pawn moves but we can easily recover the correct dtz of the
    // previous move if we know the position's WDL score.
    i32 beforeZeroingDTZ(WDLScore wdlScore) {
        switch (wdlScore) {
        case WDL_LOSS:         return -1;
        case WDL_BLESSED_LOSS: return -101;
        case WDL_CURSED_WIN:   return +101;
        case WDL_WIN:          return +1;
        case WDL_DRAW:
        default:               return 0;
        }
    }

    // Numbers in little endian used by sparseIndex[] to point into blockLength[]
    struct SparseEntry {
        char block[4];   // Number of block
        char offset[2];  // Offset within the block
    };

    static_assert (sizeof (SparseEntry) == 6, "SparseEntry size incorrect");

    using Symbol = u16; // Huffman symbol

    struct LR {

        enum Side { LEFT, RIGHT };

        // The 1st 12 bits is the left-hand symbol,
        // the 2nd 12 bits is the right-hand symbol.
        // If symbol has length 1, then the first byte is the stored value.
        u08 lr[3];

        template<Side S>
        Symbol get() {
            return
                S == Side::LEFT  ? ((lr[1] & 0xF) << 8) | lr[0] :
                S == Side::RIGHT ?  (lr[2] << 4) | (lr[1] >> 4) : (assert(false), Symbol(-1));
        }
    };

    static_assert (sizeof (LR) == 3, "LR size incorrect");


    // Tablebases data layout is structured as following:
    //
    //  TBFile: memory maps/unmaps the physical .rtbw and .rtbz files
    //  TBTable: one object for each file with corresponding indexing information
    //  TBTableDB: has ownership of TBTable objects, keeping a list and a hash

    // class TBFile memory maps/unmaps the single .rtbw and .rtbz files. Files are
    // memory mapped for best performance. Files are mapped at first access: at init
    // time only existence of the file is checked.
    class TBFile :
        public std::ifstream {

    public:

        // Look for and open the file among the Paths directories where the .rtbw and .rtbz files can be found.
        // Multiple directories are separated by ";" on Windows and by ":" on Unix-based operating systems.
        //
        // Example:
        // C:\tb\wdl345;C:\tb\wdl6;D:\tb\dtz345;D:\tb\dtz6
        static vector<string> Paths;

        string filename;

        TBFile(string const &file) {
            filename.clear();
            for (auto &path : Paths) {
                auto fname{ path + "/" + file }; //appendPath(path, file);
                std::ifstream::open(fname);
                if (is_open()) {
                    filename = fname;
                    std::ifstream::close();
                    return;
                }
            }
        }

        // Memory map the file and check it. File should be already open and will be closed after mapping.
        u08* map(void **baseAddress, u64 *mapping, TBType type) {
            assert(!filename.empty());

#       if defined(_WIN32)

            HANDLE hFile =
                CreateFile(
                    filename.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_RANDOM_ACCESS, // NOTE: FILE_FLAG_RANDOM_ACCESS is only a hint to Windows and as such may get ignored.
                    nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
                std::cerr << "CreateFile() failed, file = " << filename << std::endl;
                *baseAddress = nullptr;
                return nullptr;
            }

            DWORD hiSize;
            DWORD loSize = GetFileSize(hFile, &hiSize);

            if (loSize % 64 != 16) {
                std::cerr << "Corrupt tablebase, file = " << filename << std::endl;
                std::exit(EXIT_FAILURE);
            }

            HANDLE hFileMap =
                CreateFileMapping(
                    hFile,
                    nullptr,
                    PAGE_READONLY,
                    hiSize,
                    loSize,
                    nullptr);

            CloseHandle(hFile);

            if (hFileMap == nullptr) {
                std::cerr
                    << "CreateFileMapping() failed, file = " << filename << std::endl;
                std::exit(EXIT_FAILURE);
            }

            *mapping = (u64)hFileMap;
            *baseAddress =
                MapViewOfFile(
                    hFileMap,
                    FILE_MAP_READ,
                    0, // FileOffsetHigh
                    0, // FileOffsetLow
                    0);
            if ((*baseAddress) == nullptr) {
                std::cerr
                    << "MapViewOfFile() failed, file = " << filename << std::endl;
                std::exit(EXIT_FAILURE);
            }

#       else

            i32 hFile =
                ::open(
                    filename.c_str(),
                    O_RDONLY);
            if (hFile == -1) {
                std::cerr << "open() failed, file = " << filename << std::endl;
                *baseAddress = nullptr;
                return nullptr;
            }

            stat statbuf;
            fstat(hFile, &statbuf);

            if (statbuf.st_size == 0) {
                std::cerr << "fstat() failed, file = " << filename << std::endl;
                ::close(hFile);
                std::exit(EXIT_FAILURE);
            }

            if (statbuf.st_size % 64 != 16) {
                std::cerr << "Corrupt tablebase, file = " << filename << std::endl;
                ::close(hFile);
                std::exit(EXIT_FAILURE);
            }

            *mapping = statbuf.st_size;
            *baseAddress =
                mmap(nullptr,
                     statbuf.st_size,
                     PROT_READ,
                     MAP_SHARED,
                     hFile,
                     0);
            madvise(*baseAddress, statbuf.st_size, MADV_RANDOM);
            ::close(hFile);

            if (*baseAddress == MAP_FAILED) {
                std::cerr << "mmap() failed, file = " << filename << std::endl;
                std::exit(EXIT_FAILURE);
            }

#       endif

            u08 *data = (u08*)(*baseAddress);

            constexpr u08 TB_MAGIC[][4]
            {
                { 0xD7, 0x66, 0x0C, 0xA5 },
                { 0x71, 0xE8, 0x23, 0x5D }
            };

            if (memcmp(data, TB_MAGIC[type == WDL], 4)) {
                std::cerr << "Corrupted table, file = " << filename << std::endl;
                unmap(*baseAddress, *mapping);
                *baseAddress = nullptr;
                return nullptr;
            }

            return data + 4; // Skip Magics's header
        }

        static void unmap(void *baseAddress, u64 mapping) {

#       if defined(_WIN32)

            UnmapViewOfFile(baseAddress);
            CloseHandle((HANDLE)mapping);

#       else

            munmap(baseAddress, mapping);

#       endif

        }
    };

    vector<string> TBFile::Paths;

    /// struct PairsData contains low level indexing information to access TB data.
    /// There are 8, 4 or 2 PairsData records for each TBTable, according to type of
    /// table and if positions have pawns or not. It is populated at first access.
    struct PairsData {
        i32 flags;                  // Table flags, see enum TBFlag
        i16 maxSymLen;              // Maximum length in bits of the Huffman symbols
        i16 minSymLen;              // Minimum length in bits of the Huffman symbols
        i32 numBlocks;              // Number of blocks in the TB file
        size_t blockSize;           // Block size in bytes
        size_t span;                // About every span values there is a SparseIndex[] entry
        Symbol *lowestSym;          // lowestSym[l] is the symbol of length l with the lowest value
        LR *btree;                  // btree[sym] stores the left and right symbols that expand sym
        u16 *blockLength;           // Number of stored positions (minus one) for each block: 1..65536
        i32 blockLengthSize;        // Size of blockLength[] table: padded so it's bigger than numBlocks
        SparseEntry *sparseIndex;   // Partial indices into blockLength[]
        size_t sparseIndexSize;     // Size of sparseIndex[] table
        u08 *data;                  // Start of Huffman compressed data
        vector<u64> base64;         // base64[l - minSymLen] is the 64bit-padded lowest symbol of length l
        vector<u08> symLen;         // Number of values (-1) represented by a given Huffman symbol: 1..256
        Piece pieces[TBPIECES];     // Position pieces: the order of pieces defines the groups
        u64 groupIdx[TBPIECES + 1]; // Start index used for the encoding of the group's pieces
        i16 groupLen[TBPIECES + 1]; // Number of pieces in a given group: KRKN ->(3, 1)
        u16 mapIdx[4];              // WDLWin, WDLLoss, WDLCursedWin, WDLBlessedLoss (used in DTZ)
    };

    /// struct TBTable contains indexing information to access the corresponding TBFile.
    /// There are 2 types of TBTable, corresponding to a WDL or a DTZ file.
    /// TBTable is populated at init time but the nested PairsData records are
    /// populated at first access, when the corresponding file is memory mapped.
    template<TBType Type>
    struct TBTable {
        using Ret = typename std::conditional<Type == WDL, WDLScore, i32>::type;

        static constexpr i16 Sides{ Type == WDL ? 2 : 1 };

        std::atomic<bool> ready;
        void *baseAddress;
        u08 *map;
        u64 mapping;
        Key matlKey1;
        Key matlKey2;
        i32 pieceCount;
        bool hasPawns;
        bool hasUniquePieces;
        u08 pawnCount[COLORS]; // [Lead color / other color]
        PairsData items[Sides][4]; // [wtm / btm][FILE_A..FILE_D or 0]

        PairsData* get(i16 stm, File f) {
            return &items[stm % Sides][hasPawns ? f : 0];
        }

        TBTable() :
            ready{ false },
            baseAddress{ nullptr },
            map{ nullptr },
            mapping{ 0 }
        {}

        explicit TBTable(string const&);
        explicit TBTable(TBTable<WDL> const&);

        virtual ~TBTable() {
            if (baseAddress != nullptr) {
                TBFile::unmap(baseAddress, mapping);
            }
        }
    };

    template<>
    TBTable<WDL>::TBTable(string const &code) :
        TBTable{} {

        StateInfo si;
        Position pos;
        matlKey1 = pos.setup(code, WHITE, si).matlKey();
        pieceCount = pos.count();
        hasPawns = pos.count(PAWN) != 0;

        hasUniquePieces = false;
        for (Color c : { WHITE, BLACK }) {
            for (PieceType pt = PAWN; pt <= QUEN; ++pt) {
                if (pos.count(c|pt) == 1) {
                    hasUniquePieces = true;
                    goto exitLoop;
                }
            }
        }
        exitLoop:
        // Set the leading color. In case both sides have pawns the leading color
        // is the side with less pawns because this leads to better compression.
        bool c =
             pos.count(B_PAWN) == 0
         || (pos.count(W_PAWN) != 0
          && pos.count(B_PAWN) >= pos.count(W_PAWN));

        pawnCount[0] = u08(pos.count((c ? WHITE : BLACK)|PAWN));
        pawnCount[1] = u08(pos.count((c ? BLACK : WHITE)|PAWN));

        matlKey2 = pos.setup(code, BLACK, si).matlKey();
    }

    template<>
    TBTable<DTZ>::TBTable(TBTable<WDL> const &wdl) :
        TBTable{} {

        matlKey1 = wdl.matlKey1;
        matlKey2 = wdl.matlKey2;
        pieceCount = wdl.pieceCount;
        hasPawns = wdl.hasPawns;
        hasUniquePieces = wdl.hasUniquePieces;
        pawnCount[0] = wdl.pawnCount[0];
        pawnCount[1] = wdl.pawnCount[1];
    }

    /// class TBTableDB creates and keeps ownership of the TBTable objects,
    /// one for each TB file found. It supports a fast, hash based, table lookup.
    /// Populated at init time, accessed at probe time.
    class TBTableDB {

    private:

        struct Entry
        {
            Key key;
            TBTable<WDL>* wdl;
            TBTable<DTZ>* dtz;

            template <TBType Type>
            TBTable<Type>* get() const {
                return (TBTable<Type>*)
                        (Type == WDL ?
                            (void*)wdl : (void*)dtz);
            }
        };
        static_assert(std::is_trivially_copyable<Entry>::value, "");

        static constexpr i32 Size{ 1 << 12 }; // 4K table, indexed by key's 12 lsb

        Entry entryTable[Size + 1];

        std::deque<TBTable<WDL>> wdlTable;
        std::deque<TBTable<DTZ>> dtzTable;

        void insert(Key matlKey, TBTable<WDL> *wdl, TBTable<DTZ> *dtz) {
            u32 homeBucket = matlKey & (Size - 1);
            Entry entry{ matlKey, wdl, dtz };

            // Ensure last element is empty to avoid overflow when looking up
            for (u32 bucket = homeBucket; bucket < Size; ++bucket) {
                Key omatlKey{ entryTable[bucket].key };
                if (omatlKey == matlKey
                 || entryTable[bucket].get<WDL>() == nullptr) {
                    entryTable[bucket] = entry;
                    return;
                }

                // Robin Hood hashing: If we've probed for longer than this element,
                // insert here and search for a new spot for the other element instead.
                u32 ohomeBucket = omatlKey & (Size - 1);
                if (ohomeBucket > homeBucket) {
                    std::swap(entry, entryTable[bucket]);
                    matlKey = omatlKey;
                    homeBucket = ohomeBucket;
                }
            }

            std::cerr << "TB hash table size too low!" << std::endl;
            std::exit(EXIT_FAILURE);
        }

    public:

        template<TBType Type>
        TBTable<Type>* get(Key matlKey) {
            Entry const *entry{ &entryTable[matlKey & (Size - 1)] };
            while (true) {
                auto type{ entry->get<Type>() };
                if (entry->key == matlKey
                 || type == nullptr) {
                    return type;
                }
                ++entry;
            }
            return nullptr;
        }

        void clear() {
            std::memset(entryTable, 0, sizeof (entryTable));
            wdlTable.clear();
            dtzTable.clear();
        }

        size_t size() const {
            return wdlTable.size();
        }

        void add(vector<PieceType> const &pieces) {

            std::ostringstream oss;
            for (PieceType pt : pieces) {
                oss << toChar(pt);
            }

            string code{ oss.str() };
            code.insert(code.find('K', 1), "v");
            TBFile file{ code + ".rtbw" };
            if (file.filename.empty()) { // Only WDL file is checked
                return;
            }

            MaxPieceLimit = std::max(i16(pieces.size()), MaxPieceLimit);

            wdlTable.emplace_back(code);
            dtzTable.emplace_back(wdlTable.back());
            // Insert into the hash keys for both colors: KRvK with KR white and black
            insert(wdlTable.back().matlKey1, &wdlTable.back(), &dtzTable.back());
            insert(wdlTable.back().matlKey2, &wdlTable.back(), &dtzTable.back());
        }
    };

    TBTableDB TBTables;

    /// TB tables are compressed with canonical Huffman code. The compressed data is divided into
    /// blocks of size d->blockSize, and each block stores a variable number of symbols.
    /// Each symbol represents either a WDL or a (remapped) DTZ value, or a pair of other symbols
    /// (recursively). If you keep expanding the symbols in a block, you end up with up to 65536
    /// WDL or DTZ values. Each symbol represents up to 256 values and will correspond after
    /// Huffman coding to at least 1 bit. So a block of 32 bytes corresponds to at most
    /// 32 x 8 x 256 = 65536 values. This maximum is only reached for tables that consist mostly
    /// of draws or mostly of wins, but such tables are actually quite common. In principle, the
    /// blocks in WDL tables are 64 bytes long (and will be aligned on cache lines). But for
    /// mostly-draw or mostly-win tables this can leave many 64-byte blocks only half-filled, so
    /// in such cases blocks are 32 bytes long. The blocks of DTZ tables are up to 1024 bytes long.
    /// The generator picks the size that leads to the smallest table. The "book" of symbols and
    /// Huffman codes is the same for all blocks in the table. A non-symmetric pawnless TB file
    /// will have one table for wtm and one for btm, a TB file with pawns will have tables per
    /// file a,b,c,d also in this case one set for wtm and one for btm.
    i32 decompressPairs(PairsData *d, u64 idx) {
        // Special case where all table positions store the same value
        if ((d->flags & TBFlag::SINGLE_VALUE) != 0) {
            return d->minSymLen;
        }

        // First we need to locate the right block that stores the value at index "idx".
        // Because each block n stores blockLength[n] + 1 values, the index i of the block
        // that contains the value at position idx is:
        //
        //     for (i = -1, sum = 0; sum <= idx; ++i)
        //         sum += blockLength[i + 1] + 1;
        //
        // This can be slow, so we use SparseIndex[] populated with a set of SparseEntry that
        // point to known indices into blockLength[]. Namely SparseIndex[k] is a SparseEntry
        // that stores the blockLength[] index and the offset within that block of the value
        // with index I(k), where:
        //
        //     I(k) = k * d->span + d->span / 2      (1)

        // First step is to get the 'k' of the I(k) nearest to our idx, using definition (1)
        u32 k{ u32(idx / d->span) };

        // Then we read the corresponding SparseIndex[] entry
        u32 block { number<u32, true>(&d->sparseIndex[k].block) };
        i32 offset{ number<u16, true>(&d->sparseIndex[k].offset) };

        // Now compute the difference idx - I(k). From definition of k we know that
        //
        //     idx = k * d->span + idx % d->span    (2)
        //
        // So from (1) and (2) we can compute idx - I(K):
        i32 diff = i32(idx % d->span - d->span / 2);

        // Sum the above to offset to find the offset corresponding to our idx
        offset += diff;

        // Move to previous/next block, until we reach the correct block that contains idx,
        // that is when 0 <= offset <= d->blockLength[block]
        while (offset < 0) {
            offset += d->blockLength[--block] + 1;
        }

        while (offset > d->blockLength[block]) {
            offset -= d->blockLength[block++] + 1;
        }

        // Finally, we find the start address of our block of canonical Huffman symbols
        u32 *ptr = (u32*)(d->data + ((u64) block * d->blockSize));

        // Read the first 64 bits in our block, this is a (truncated) sequence of
        // unknown number of symbols of unknown length but we know the first one
        // is at the beginning of this 64 bits sequence.
        u64 buf64{ number<u64, false>(ptr) }; ptr += 2;
        i32 buf64Size{ 64 };
        Symbol sym;

        while (true) {
            i32 len{ 0 }; // This is the symbol length - d->minSymLen

            // Now get the symbol length. For any symbol s64 of length l right-padded
            // to 64 bits we know that d->base64[l-1] >= s64 >= d->base64[l] so we
            // can find the symbol length iterating through base64[].
            while (buf64 < d->base64[len]) {
                ++len;
            }

            // All the symbols of a given length are consecutive integers (numerical
            // sequence property), so we can compute the offset of our symbol of
            // length len, stored at the beginning of buf64.
            sym = Symbol((buf64 - d->base64[len]) >> (64 - len - d->minSymLen));

            // Now add the value of the lowest symbol of length len to get our symbol
            sym += number<Symbol, true>(&d->lowestSym[len]);

            // If our offset is within the number of values represented by symbol sym
            if (offset < d->symLen[sym] + 1) {
                break;
            }

            // ...otherwise update the offset and continue to iterate
            offset -= d->symLen[sym] + 1;
            len += d->minSymLen;  // Get the real length
            buf64 <<= len;          // Consume the just processed symbol
            buf64Size -= len;

            if (buf64Size <= 32) {
                // Refill the buffer
                buf64Size += 32;
                buf64 |= (u64)(number<u32, false>(ptr++)) << (64 - buf64Size);
            }
        }

        // Ok, now we have our symbol that expands into d->symLen[sym] + 1 symbols.
        // We binary-search for our value recursively expanding into the left and
        // right child symbols until we reach a leaf node where symLen[sym] + 1 == 1
        // that will store the value we need.
        while (d->symLen[sym] != 0) {
            Symbol left{ d->btree[sym].get<LR::Side::LEFT>() };

            // If a symbol contains 36 sub-symbols (d->symLen[sym] + 1 = 36) and
            // expands in a pair (d->symLen[left] = 23, d->symLen[right] = 11), then
            // we know that, for instance the ten-th value (offset = 10) will be on
            // the left side because in Recursive Pairing child symbols are adjacent.
            if (offset < d->symLen[left] + 1) {
                sym = left;
            }
            else {
                offset -= d->symLen[left] + 1;
                sym = d->btree[sym].get<LR::Side::RIGHT>();
            }
        }

        return d->btree[sym].get<LR::Side::LEFT>();
    }

    bool checkDTZStm(TBTable<WDL>*, Color, File) {
        return true;
    }

    bool checkDTZStm(TBTable<DTZ> *entry, Color stm, File f) {
        auto flags = entry->get(stm, f)->flags;
        return (flags & TBFlag::STM) == stm
            || ((entry->matlKey1 == entry->matlKey2)
             && !entry->hasPawns);
    }

    /// DTZ scores are sorted by frequency of occurrence and then assigned the
    /// values 0, 1, 2, ... in order of decreasing frequency. This is done for each
    /// of the four WDLScore values. The mapping information necessary to reconstruct
    /// the original values is stored in the TB file and read during map[] init.
    WDLScore mapScore(TBTable<WDL>*, File, i32 value, WDLScore) {
        return WDLScore(value - 2);
    }

    constexpr i32 WDLMap[]{ 1, 3, 0, 2, 0 };

    i32 mapScore(TBTable<DTZ> *entry, File f, i32 value, WDLScore wdl) {

        i32 flags = entry->get(0, f)->flags;
        u08 *map = entry->map;
        u16* idx = entry->get(0, f)->mapIdx;

        if ((flags & TBFlag::MAPPED) != 0) {
            value = (flags & TBFlag::WIDE) != 0 ?
                    ((u16*)map)[idx[WDLMap[wdl + 2]] + value] :
                    map[idx[WDLMap[wdl + 2]] + value];
        }

        // DTZ tables store distance to zero in number of moves or plies. We
        // want to return plies, so we have convert to plies when needed.
        if ((wdl == WDL_WIN  && (flags & TBFlag::WIN_PLIES) == 0)
         || (wdl == WDL_LOSS && (flags & TBFlag::LOSS_PLIES) == 0)
         ||  wdl == WDL_CURSED_WIN
         ||  wdl == WDL_BLESSED_LOSS) {
            value *= 2;
        }

        return value + 1;
    }

    /// Compute a unique index out of a position and use it to probe the TB file. To
    /// encode k pieces of same type and color, first sort the pieces by square in
    /// ascending order s1 <= s2 <= ... <= sk then compute the unique index as:
    ///
    ///      idx = Binomial[1][s1] + Binomial[2][s2] + ... + Binomial[k][sk]
    ///
    template<typename T, typename Ret = typename T::Ret>
    Ret doProbeTable(Position const &pos, T *entry, WDLScore wdl, ProbeState &state) {

        Square squares[TBPIECES];
        Piece  pieces [TBPIECES];
        i16 size{ 0 };

        bool flip{
            // Black Symmetric
            // A given TB entry like KRK has associated two material keys: KRvK and KvKR.
            // If both sides have the same pieces keys are equal. In this case TB tables
            // only store the 'white to move' case, so if the position to lookup has black
            // to move, we need to switch the color and flip the squares before to lookup.
            (pos.activeSide() == BLACK
          && entry->matlKey1 == entry->matlKey2)
            // Black Stronger
            // TB files are calculated for white as stronger side. For instance we have
            // KRvK, not KvKR. A position where stronger side is white will have its
            // material key == entry->matlKey1, otherwise we have to switch the color and
            // flip the squares before to lookup.
         || (pos.matlKey() != entry->matlKey1) };

        Color stm{ pos.activeSide() };
        if (flip) stm = ~stm;

        Bitboard pawns;
        i16 pawnCount;
        File pawnFile;
        // For pawns, TB files store 4 separate tables according if leading pawn is on
        // file a, b, c or d after reordering. The leading pawn is the one with maximum
        // MapPawns[] value, that is the one most toward the edges and with lowest rank.
        if (entry->hasPawns) {
            // In all the 4 tables, pawns are at the beginning of the piece sequence and
            // their color is the reference one. So we just pick the first one.
            Piece p{ Piece(entry->get(0, FILE_A)->pieces[0]) };
            assert(pType(p) == PAWN);
            if (flip) p = flipColor(p);

            pawns = pos.pieces(pColor(p), PAWN);

            Bitboard b{ pawns };
            assert(b != 0);
            do {
                auto s{ popLSq(b) };
                if (flip) s = flipRank(s);
                squares[size] = s;

                ++size;
            } while (b != 0);
            pawnCount = size;

            std::swap(squares[0], *std::max_element(squares, squares + pawnCount, mapPawnsCompare));
            pawnFile = File(edgeDistance(sFile(squares[0])));
        }
        else {
            pawns = 0;
            pawnCount = 0;
            pawnFile = FILE_A;
        }

        // DTZ tables are one-sided, i.e. they store positions only for white to
        // move or only for black to move, so check for side to move to be color,
        // early exit otherwise.
        if (!checkDTZStm(entry, stm, pawnFile)) {
            state = PS_OPP_SIDE;
            return Ret();
        }

        // Now we are ready to get all the position pieces(but the lead pawns) and
        // directly map them to the correct color and square.
        Bitboard b{ pos.pieces() ^ pawns };
        assert(b != 0);
        do {
            auto s{ popLSq(b) };
            auto p{ pos[s] };
            if (flip) {
                s = flipRank(s);
                p = flipColor(p);
            }
            squares[size] = s;
            pieces [size] = p;

            ++size;
        } while (b != 0);

        assert(size >= 2);

        PairsData *d = entry->get(stm, pawnFile);

        // Then we reorder the pieces to have the same sequence as the one stored
        // in pieces[i]: the sequence that ensures the best compression.
        for (i16 i = pawnCount; i < size - 1; ++i) {
            for (i16 j = i + 1; j < size; ++j) {
                if (d->pieces[i] == pieces[j]) {
                    std::swap(pieces[i], pieces[j]);
                    std::swap(squares[i], squares[j]);
                    break;
                }
            }
        }

        // Now we map again the squares so that the square of the lead piece is in
        // the triangle A1-D1-D4.
        if (sFile(squares[0]) > FILE_D) {
            for (i16 i = 0; i < size; ++i) {
                squares[i] = flipFile(squares[i]);
            }
        }

        u64 idx;
        // Encode leading pawns starting with the one with minimum MapPawns[] and
        // proceeding in ascending order.
        if (entry->hasPawns) {
            idx = LeadPawnIdx[pawnCount][squares[0]];

            std::sort(squares + 1, squares + pawnCount, mapPawnsCompare);

            for (i16 i = 1; i < pawnCount; ++i) {
                idx += Binomial[i][MapPawns[squares[i]]];
            }

            goto encodeRemaining; // With pawns we have finished special treatments
        }

        // In positions without pawns:
        // Flip the squares to ensure leading piece is below RANK_5.
        if (sRank(squares[0]) > RANK_4) {
            for (i32 i = 0; i < size; ++i) {
                squares[i] = flipRank(squares[i]);
            }
        }
        // Look for the first piece of the leading group not on the A1-D4 diagonal
        // and ensure it is mapped below the diagonal.
        for (i16 i = 0; i < d->groupLen[0]; ++i) {
            if (offA1H8(squares[i]) > 0) { // A1-H8 diagonal flip: SQ_A3 -> SQ_C1
                for (i32 j = i; j < size; ++j) {
                    squares[j] = Square(((squares[j] >> 3) | (squares[j] << 3)) & 63);
                }
            }
        }

        // Encode the leading group.
        //
        // Suppose we have KRvK. Let's say the pieces are on square numbers wK, wR
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
        // Once we have placed the wK and bK, there are 62 squares left for the wR
        // Mapping its square from 0..63 to available squares 0..61 can be done like:
        //
        //   wR -= (wR > wK) + (wR > bK);
        //
        // In words: if wR "comes later" than wK, we deduct 1, and the same if wR
        // "comes later" than bK. In case of two same pieces like KRRvK we want to
        // place the two Rs "together". If we have 62 squares left, we can place two
        // Rs "together" in 62 * 61 / 2 ways (we divide by 2 because rooks can be
        // swapped and still get the same position.)
        //
        // In case we have at least 3 unique pieces(included kings) we encode them together.
        if (entry->hasUniquePieces) {
            i32 adjust1{ (squares[1] > squares[0]) };
            i32 adjust2{ (squares[2] > squares[0])
                       + (squares[2] > squares[1]) };

            // First piece is below a1-h8 diagonal. MapA1D1D4[] maps the b1-d1-d3 triangle to 0...5.
            // There are 63 squares (mapped to 0...62) for the second piece
            //       and 62 squares (mapped to 0...61) for the third piece.
            if (offA1H8(squares[0]) != 0) {
                idx =                         0 * 63 * 62
                  + MapA1D1D4[squares[0]]       * 63 * 62
                  + (squares[1] - adjust1)           * 62
                  + (squares[2] - adjust2);
            }
            // First piece is on a1-h8 diagonal, second below:
            // map this occurrence to 6 to differentiate from the above case,
            // rank() maps a1-d4 diagonal to 0...3 and
            // finally MapB1H1H7[] maps the b1-h1-h7 triangle to 0..27.
            else
            if (offA1H8(squares[1]) != 0) {
                idx =                         6 * 63 * 62
                  + (sRank(squares[0]))         * 28 * 62
                  + MapB1H1H7[squares[1]]            * 62
                  + (squares[2] - adjust2);
            }
            // First two pieces are on a1-h8 diagonal, third below
            else
            if (offA1H8(squares[2]) != 0) {
                idx =                         6 * 63 * 62
                  +                           4 * 28 * 62
                  + (sRank(squares[0]))         *  7 * 28
                  + (sRank(squares[1]) - adjust1)    * 28
                  + MapB1H1H7[squares[2]];
            }
            // All 3 pieces on the diagonal a1-h8
            else {
                idx =                         6 * 63 * 62
                  +                           4 * 28 * 62
                  +                           4 *  7 * 28
                  + (sRank(squares[0]))         *  7 *  6
                  + (sRank(squares[1]) - adjust1)    *  6
                  + (sRank(squares[2]) - adjust2);
            }
        }
        else {
            // We don't have at least 3 unique pieces, like in KRRvKBB, just map the kings.
            idx = MapKK[MapA1D1D4[squares[0]]][squares[1]];
        }

    encodeRemaining:

        idx *= d->groupIdx[0];
        Square *groupSq = squares + d->groupLen[0];

        // Encode remaining pawns then pieces according to square, in ascending order
        bool pawnRemains{
               entry->hasPawns
            && entry->pawnCount[1] };

        i16 next{ 0 };
        while (d->groupLen[++next] != 0) {

            assert(0 <= d->groupLen[next] && d->groupLen[next] < TBPIECES);

            std::sort(groupSq, groupSq + d->groupLen[next]);
            u64 n = 0;

            // Map down a square if "comes later" than a square in the previous
            // groups (similar to what done earlier for leading group pieces).
            for (i16 i = 0; i < d->groupLen[next]; ++i) {
                auto adjust =
                    std::count_if(squares, groupSq,
                        [&](Square s) {
                            return groupSq[i] > s;
                        });
                n += Binomial[i + 1][groupSq[i] - 1 * adjust - 8 * pawnRemains];
            }

            pawnRemains = false;
            idx += n * d->groupIdx[next];
            groupSq += d->groupLen[next];
        }

        // Now that we have the index, decompress the pair and get the score
        return mapScore(entry, pawnFile, decompressPairs(d, idx), wdl);
    }

    /// Group together pieces that will be encoded together. The general rule is that
    /// a group contains pieces of same type and color. The exception is the leading
    /// group that, in case of positions without pawns, can be formed by 3 different
    /// pieces(default) or by the king pair when there is not a unique piece apart
    /// from the kings. When there are pawns, pawns are always first in pieces[].
    ///
    /// As example KRKN -> KRK + N, KNNK -> KK + NN, KPPKP -> P + PP + K + K
    ///
    /// The actual grouping depends on the TB generator and can be inferred from the
    /// sequence of pieces in piece[] array.
    template<typename T>
    void setGroups(T &e, PairsData *d, i16 *order, File f) {
        i16 firstLen =
            e.hasPawns ?            0 :
                e.hasUniquePieces ? 3 : 2;

        i16 n{ 0 };
        d->groupLen[n] = 1;
        // Number of pieces per group is stored in groupLen[], for instance in KRKN
        // the encoder will default on '111', so groupLen[] will be (3, 1).
        for (i32 i = 1; i < e.pieceCount; ++i) {
            if (--firstLen > 0
             || d->pieces[i] == d->pieces[i - 1]) {
                d->groupLen[n]++;
            }
            else {
                ++n;
                d->groupLen[n] = 1;
            }
        }
        ++n;
        d->groupLen[n] = 0; // Zero-terminated

        // The sequence in pieces[] defines the groups, but not the order in which
        // they are encoded. If the pieces in a group g can be combined on the board
        // in N(g) different ways, then the position encoding will be of the form:
        //
        //           g1 * N(g2) * N(g3) + g2 * N(g3) + g3
        //
        // This ensures unique encoding for the whole position. The order of the
        // groups is a per-table parameter and could not follow the canonical leading
        // pawns/pieces -> remainig pawns -> remaining pieces. In particular the
        // first group is at order[0] position and the remaining pawns, when present,
        // are at order[1] position.

        // Pawns on both sides
        bool pp{ e.hasPawns
              && e.pawnCount[1] };
        i16 next = 1 + pp;
        i32 emptyCount{ 64 - d->groupLen[0] - (pp ? d->groupLen[1] : 0) };

        u64 idx = 1;
        for (i16 k = 0;
            next < n
         || k == order[0]
         || k == order[1];
            ++k) {
            // Leading pawns or pieces
            if (k == order[0]) {
                d->groupIdx[0] = idx;
                idx *= e.hasPawns        ? LeadPawnsSize[d->groupLen[0]][f] :
                       e.hasUniquePieces ? 31332 : 462;
            }
            else
            // Remaining pawns
            if (k == order[1]) {
                d->groupIdx[1] = idx;
                idx *= Binomial[d->groupLen[1]][48 - d->groupLen[0]];
            }
            // Remaining pieces
            else {
                d->groupIdx[next] = idx;
                idx *= Binomial[d->groupLen[next]][emptyCount];
                emptyCount -= d->groupLen[next];
                ++next;
            }
        }
        d->groupIdx[n] = idx;
    }

    /// In Recursive Pairing each symbol represents a pair of children symbols. So
    /// read d->btree[] symbols data and expand each one in his left and right child
    /// symbol until reaching the leafs that represent the symbol value.
    u08 setSymLen(PairsData *d, Symbol s, vector<bool> &visited) {

        visited[s] = true; // We can set it now because tree is acyclic

        Symbol symR{ d->btree[s].get<LR::Side::RIGHT>() };
        if (symR == 0xFFF) {
            return 0;
        }

        Symbol symL{ d->btree[s].get<LR::Side::LEFT>() };
        if (!visited[symL]) {
            d->symLen[symL] = setSymLen(d, symL, visited);
        }
        if (!visited[symR]) {
            d->symLen[symR] = setSymLen(d, symR, visited);
        }

        return d->symLen[symL] + d->symLen[symR] + 1;
    }

    u08* setSizes(PairsData *d, u08 *data) {

        d->flags = *data++;

        if ((d->flags & TBFlag::SINGLE_VALUE) != 0) {
            d->numBlocks = 0;
            d->blockLengthSize = 0;
            d->span = 0;
            d->sparseIndexSize = 0;
            d->minSymLen = *data++; // Here we store the single value
            return data;
        }

        // groupLen[] is a zero-terminated list of group lengths, the last groupIdx[]
        // element stores the biggest index that is the tb size.
        u64 tbSize = d->groupIdx[std::find(d->groupLen, d->groupLen + TBPIECES, 0) - d->groupLen];

        d->blockSize = u64(1) << *data++;
        d->span = u64(1) << *data++;
        d->sparseIndexSize = (tbSize + d->span - 1) / d->span; // Round up
        auto padding = number<u08, true>(data); data += 1;
        d->numBlocks = number<u32, true>(data); data += sizeof (u32);
        d->blockLengthSize = d->numBlocks + padding; // Padded to ensure SparseIndex[] does not point out of range.

        d->maxSymLen = *data++;
        d->minSymLen = *data++;
        d->lowestSym = (Symbol*)(data);

        i16 const base64Size = d->maxSymLen - d->minSymLen + 1;
        d->base64.resize(base64Size);

        // The canonical code is ordered such that longer symbols (in terms of
        // the number of bits of their Huffman code) have lower numeric value,
        // so that d->lowestSym[i] >= d->lowestSym[i+1] (when read as LittleEndian).
        // Starting from this we compute a base64[] table indexed by symbol length
        // and containing 64 bit values so that d->base64[i] >= d->base64[i+1].
        // See http://www.eecs.harvard.edu/~michaelm/E210/huffman.pdf
        for (i16 i = base64Size - 2; i >= 0; --i) {
            d->base64[i] =
                (d->base64[i + 1]
               + number<Symbol, true>(&d->lowestSym[i])
               - number<Symbol, true>(&d->lowestSym[i + 1])) / 2;

            assert(d->base64[i] * 2 >= d->base64[i + 1]);
        }

        // Now left-shift by an amount so that d->base64[i] gets shifted 1 bit more
        // than d->base64[i+1] and given the above assert condition, we ensure that
        // d->base64[i] >= d->base64[i+1]. Moreover for any symbol s64 of length i
        // and right-padded to 64 bits holds d->base64[i-1] >= s64 >= d->base64[i].
        for (i16 i = 0; i < base64Size; ++i) {
            d->base64[i] <<= 64 - i - d->minSymLen; // Right-padding to 64 bits
        }

        data += base64Size * sizeof (Symbol);
        d->symLen.resize(number<u16, true>(data)); data += sizeof (u16);
        d->btree = (LR*)(data);

        // The compression scheme used is "Recursive Pairing", that replaces the most
        // frequent adjacent pair of symbols in the source message by a new symbol,
        // reevaluating the frequencies of all of the symbol pairs with respect to
        // the extended alphabet, and then repeating the process.
        // See http://www.larsson.dogma.net/dcc99.pdf
        vector<bool> visited(d->symLen.size());
        for (Symbol sym = 0; sym < d->symLen.size(); ++sym) {
            if (!visited[sym]) {
                d->symLen[sym] = setSymLen(d, sym, visited);
            }
        }

        return data
             + d->symLen.size() * sizeof (LR)
             + (d->symLen.size() & 1);
    }

    u08* setDTZMap(TBTable<WDL>&, u08 *data, File) {
        return data;
    }

    u08* setDTZMap(TBTable<DTZ> &e, u08 *data, File maxFile) {

        e.map = data;
        for (File f = FILE_A; f <= maxFile; ++f) {
            auto flags{ e.get(0, f)->flags };

            if ((TBFlag::MAPPED & flags) != 0) {
                if ((TBFlag::WIDE & flags) != 0) {
                    data += uPtr(data) & 1; // Word alignment, we may have a mixed table
                    // Sequence like 3,x,x,x,1,x,0,2,x,x
                    for (i16 i = 0; i < 4; ++i) {
                        e.get(0, f)->mapIdx[i] = u16((u16*)(data) - (u16*)(e.map) + 1);
                        data += 2 * number<u16, true>(data) + 2;
                    }
                }
                else {
                    for (i16 i = 0; i < 4; ++i) {
                        e.get(0, f)->mapIdx[i] = (u16)(data - e.map + 1);
                        data += *data + 1;
                    }
                }
            }
        }

        data += (uPtr)data & 1; // Word alignment
        return data;
    }

    // Populate entry's PairsData records with data from the just memory mapped file.
    // Called at first access.
    template<typename T>
    void set(T &e, u08 *data) {

        assert(e.hasPawns                 == bool(*data & 2)); // HasPawns
        assert((e.matlKey1 != e.matlKey2) == bool(*data & 1)); // Split

        data++; // First byte stores flags

        i16 const sides =
            T::Sides == 2
         && (e.matlKey1 != e.matlKey2) ?
                2 : 1;
        File const maxFile{
            e.hasPawns ?
                FILE_D : FILE_A };

        // Pawns on both sides
        bool pp{ e.hasPawns
              && e.pawnCount[1] };
        assert(!pp || e.pawnCount[0]);

        for (File f = FILE_A; f <= maxFile; ++f) {
            for (i16 i = 0; i < sides; ++i) {
                *e.get(i, f) = PairsData{};
            }

            i16 order[][2]
            {
                { i16(*(data) & 0xF), i16(pp ? *(data + 1) & 0xF : 0xF) },
                { i16(*(data) >>  4), i16(pp ? *(data + 1) >>  4 : 0xF) }
            };

            data += 1 + pp;

            for (i32 k = 0; k < e.pieceCount; ++k, ++data) {
                for (i16 i = 0; i < sides; ++i) {
                    e.get(i, f)->pieces[k] = Piece(i != 0 ? *data >> 4 : *data & 0xF);
                }
            }

            for (i16 i = 0; i < sides; ++i) {
                setGroups(e, e.get(i, f), order[i], f);
            }
        }

        data += uPtr(data) & 1; // Word alignment

        for (File f = FILE_A; f <= maxFile; ++f) {
            for (i16 i = 0; i < sides; ++i) {
                data = setSizes(e.get(i, f), data);
            }
        }

        data = setDTZMap(e, data, maxFile);

        PairsData *d;
        for (File f = FILE_A; f <= maxFile; ++f) {
            for (i16 i = 0; i < sides; ++i) {
                (d = e.get(i, f))->sparseIndex = (SparseEntry*)(data);
                data += d->sparseIndexSize * sizeof (SparseEntry);
            }
        }
        for (File f = FILE_A; f <= maxFile; ++f) {
            for (i16 i = 0; i < sides; ++i) {
                (d = e.get(i, f))->blockLength = (u16*)(data);
                data += d->blockLengthSize * sizeof (u16);
            }
        }
        for (File f = FILE_A; f <= maxFile; ++f) {
            for (i16 i = 0; i < sides; ++i) {
                data = (u08*)(((uPtr)data + 0x3F) & ~0x3F); // 64 byte alignment
                (d = e.get(i, f))->data = data;
                data += d->numBlocks * d->blockSize;
            }
        }
    }

    // If the TB file corresponding to the given position is already memory mapped
    // then return its base address, otherwise try to memory map and init it. Called
    // at every probe, memory map and init only at first access. Function is thread
    // safe and can be called concurrently.
    template<TBType Type>
    void* mapped(TBTable<Type> &e, Position const &pos) {
        static std::mutex mutex;

        // Use 'acquire' to avoid a thread reading 'ready' == true while
        // another is still working. (compiler reordering may cause this).
        if (e.ready.load(std::memory_order::memory_order_acquire)) {
            return e.baseAddress; // Could be nullptr if file does not exist
        }

        std::unique_lock<std::mutex> lock(mutex);

        if (e.ready.load(std::memory_order::memory_order_relaxed)) { // Recheck under lock
            return e.baseAddress;
        }

        // Pieces strings in decreasing order for each color, like ("KPP","KR")
        string w, b;
        for (PieceType pt = KING; pt >= PAWN; --pt) {
            w += string(pos.count(WHITE|pt), toChar(pt));
            b += string(pos.count(BLACK|pt), toChar(pt));
        }

        string code{ pos.matlKey() == e.matlKey1 ? w + 'v' + b : b + 'v' + w };

        u08 *data{ TBFile{ code + (Type == WDL ? ".rtbw" : ".rtbz") }.map(&e.baseAddress, &e.mapping, Type) };
        if (data != nullptr) {
            set(e, data);
        }

        e.ready.store(true, std::memory_order::memory_order_release);
        return e.baseAddress;
    }

    template<TBType Type, typename Ret = typename TBTable<Type>::Ret>
    Ret probeTable(Position const &pos, ProbeState &state, WDLScore wdl = WDL_DRAW) {

        // KvK
        //if (pos.count() == 2) {
        if ((pos.pieces() ^ pos.pieces(KING)) == 0) {
            //state = PS_SUCCESS;
            return Ret(WDL_DRAW);
        }

        TBTable<Type> *entry{ TBTables.get<Type>(pos.matlKey()) };

        if (entry == nullptr
         || mapped(*entry, pos) == nullptr) {
            state = PS_FAILURE;
            return Ret();
        }

        return doProbeTable(pos, entry, wdl, state);
    }

    /// For a position where the side to move has a winning capture it is not necessary
    /// to store a winning value so the generator treats such positions as "don't cares"
    /// and tries to assign to it a value that improves the compression ratio. Similarly,
    /// if the side to move has a drawing capture, then the position is at least drawn.
    /// If the position is won, then the TB needs to store a win value. But if the
    /// position is drawn, the TB may store a loss value if that is better for compression.
    /// All of this means that during probing, the engine must look at captures and probe
    /// their results and must probe the position itself. The "best" state of these
    /// probes is the correct state for the position.
    /// DTZ table don't store values when a following move is a zeroing winning move
    /// (winning capture or winning pawn move). Also DTZ store wrong values for positions
    /// where the best move is an ep-move(even if losing). So in all these cases set
    /// the state to PS_ZEROING.
    WDLScore search(Position &pos, ProbeState &state, bool checkZeroing) {

        WDLScore wdlBestScore{ WDL_LOSS };

        WDLScore wdlScore;
        StateInfo si;
        auto moveList{ MoveList<LEGAL>(pos) };
        u16 moveCount{ 0 };
        for (auto &move : moveList) {
            if (!pos.capture(move)
             && (!checkZeroing
              || pType(pos[orgSq(move)]) != PAWN)) {
                continue;
            }

            ++moveCount;

            pos.doMove(move, si);
            wdlScore = -search(pos, state, false);
            pos.undoMove(move);

            if (state == PS_FAILURE) {
                return WDL_DRAW;
            }

            if (wdlBestScore < wdlScore) {
                wdlBestScore = wdlScore;

                if (wdlScore >= WDL_WIN) {
                    state = PS_ZEROING; // Winning DTZ-zeroing move
                    return wdlScore;
                }
            }
        }

        // In case we have already searched all the legal moves we don't have to probe
        // the TB because the stored score could be wrong. For instance TB tables
        // do not contain information on position with Enpassant rights, so in this case
        // the state of probe_wdl_table is wrong. Also in case of only capture
        // moves, for instance here 4K3/4q3/6p1/2k5/6p1/8/8/8 w - - 0 7, we have to
        // return with PS_ZEROING set.
        bool completed = moveCount != 0
                      && moveCount == moveList.size();

        if (completed) {
            wdlScore = wdlBestScore;
        }
        else {
            wdlScore = probeTable<WDL>(pos, state);
            if (state == PS_FAILURE) {
                return WDL_DRAW;
            }
        }

        // DTZ stores a "don't care" wdlScore if wdlBestScore is a win
        if (wdlBestScore >= wdlScore) {
            state = completed
                 || wdlBestScore > WDL_DRAW ?
                    PS_ZEROING : PS_SUCCESS;
            return wdlBestScore;
        }

        state = PS_SUCCESS;
        return wdlScore;
    }

} // namespace

namespace SyzygyTB {

    i16 MaxPieceLimit;

    WDLScore operator-(WDLScore wdl) { return WDLScore(-i32(wdl)); }

    std::ostream& operator<<(std::ostream &os, WDLScore wdlScore) {
        switch (wdlScore) {
        case WDL_LOSS:         os << "Loss";         break;
        case WDL_BLESSED_LOSS: os << "Blessed Loss"; break;
        case WDL_DRAW:         os << "Draw";         break;
        case WDL_CURSED_WIN:   os << "Cursed win";   break;
        case WDL_WIN:          os << "Win";          break;
        }
        return os;
    }

    std::ostream& operator<<(std::ostream &os, ProbeState probeState) {
        switch (probeState) {
        case PS_OPP_SIDE:  os << "Opponent side";        break;
        case PS_FAILURE:   os << "Failure";              break;
        case PS_SUCCESS:   os << "Success";              break;
        case PS_ZEROING:   os << "Best move zeroes DTZ"; break;
        }
        return os;
    }

    /// Probe the WDL table for a particular position.
    /// If state != PS_FAILURE, the probe was successful.
    /// The return value is from the point of view of the side to move:
    /// -2 : loss
    /// -1 : loss, but draw under 50-move rule
    ///  0 : draw
    ///  1 : win, but draw under 50-move rule
    ///  2 : win
    WDLScore probeWDL(Position &pos, ProbeState &state) {

        state = PS_SUCCESS;
        return search(pos, state, false);
    }

    /// Probe the DTZ table for a particular position.
    /// If *result != PS_FAILURE, the probe was successful.
    /// The return value is from the point of view of the side to move:
    ///         n < -100 : loss, but draw under 50-move rule
    /// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
    ///         0        : draw
    ///     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
    ///   100 < n        : win, but draw under 50-move rule
    ///
    /// The return value n can be off by 1: a return value -n can mean a loss
    /// in n+1 ply and a return value +n can mean a win in n+1 ply. This
    /// cannot happen for tables with positions exactly on the "edge" of
    /// the 50-move rule.
    ///
    /// This implies that if dtz > 0 is returned, the position is certainly
    /// a win if dtz + 50-move-counter <= 99. Care must be taken that the engine
    /// picks moves that preserve dtz + 50-move-counter <= 99.
    ///
    /// If n = 100 immediately after a capture or pawn move, then the position
    /// is also certainly a win, and during the whole phase until the next
    /// capture or pawn move, the inequality to be preserved is
    /// dtz + 50-move counter <= 100.
    ///
    /// In short, if a move is available resulting in dtz + 50-move-counter <= 99,
    /// then do not accept moves leading to dtz + 50-move-counter == 100.
    i32 probeDTZ(Position &pos, ProbeState &state) {

        state = PS_SUCCESS;
        WDLScore wdlScore{ search(pos, state, true) };

        if (state == PS_FAILURE
         || wdlScore == WDL_DRAW) { // DTZ tables don't store draws
            return 0;
        }

        // DTZ stores a 'don't care' value in this case, or even a plain wrong
        // one as in case the best move is a losing Enpassant, so it cannot be probed.
        if (state == PS_ZEROING) {
            return beforeZeroingDTZ(wdlScore);
        }

        i32 dtz = probeTable<DTZ>(pos, state, wdlScore);

        if (state == PS_FAILURE) {
            return 0;
        }

        if (state != PS_OPP_SIDE) {
            return sign(wdlScore)
                 * (dtz
                  + 100 * (wdlScore == WDL_BLESSED_LOSS
                        || wdlScore == WDL_CURSED_WIN));
        }

        // DTZ stores results for the other side, so we need to do a 1-ply search and
        // find the winning move that minimizes DTZ.
        StateInfo si;
        i32 minDTZ = 0xFFFF;

        for (auto const &vm : MoveList<LEGAL>(pos)) {

            bool zeroing = pos.capture(vm)
                        || pType(pos[orgSq(vm)]) == PAWN;

            pos.doMove(vm, si);

            // For zeroing moves we want the dtz of the move _before_ doing it,
            // otherwise we will get the dtz of the next move sequence. Search the
            // position after the move to get the score sign(because even in a
            // winning position we could make a losing capture or going for a draw).
            dtz = zeroing ?
                    -beforeZeroingDTZ(search(pos, state, false)) :
                    -probeDTZ(pos, state);

            // If the move mates, force minDTZ to 1
            if (dtz == 1
             && pos.checkers() != 0
             && MoveList<LEGAL>(pos).size() == 0) {
                minDTZ = 1;
            }

            // Convert state from 1-ply search. Zeroing moves are already accounted
            // by beforeZeroingDTZ() that returns the DTZ of the previous move.
            if (!zeroing) {
                dtz += sign(dtz);
            }

            // Skip the draws and if we are winning only pick positive dtz
            if (sign(dtz) == sign(wdlScore)) {
                minDTZ = std::min(dtz, minDTZ);
            }

            pos.undoMove(vm);

            if (state == PS_FAILURE) {
                return 0;
            }
        }

        // When there are no legal moves, the position is mate: return -1
        return minDTZ == 0xFFFF ? -1 : minDTZ;
    }

    constexpr i16 wdlToRank[]
    {
        -1000,
        -899,
        0,
        +899,
        +1000
    };
    constexpr Value wdlToValue[]
    {
        -VALUE_MATE_1_MAX_PLY + 1,
        VALUE_DRAW - 2,
        VALUE_DRAW,
        VALUE_DRAW + 2,
        +VALUE_MATE_1_MAX_PLY - 1
    };

    /// Use the WDL tables to filter out moves that don't preserve the win or draw.
    /// This is a fall back for the case that some or all DTZ tables are missing.
    ///
    /// A return value false indicates that not all probes were successful and that
    /// no moves were filtered out.
    bool rootProbeWDL(Position &rootPos, RootMoves &rootMoves) {

        bool move50Rule{ Options["SyzygyMove50Rule"] };

        StateInfo si;
        ProbeState state;
        // Probe and rank each move
        for (auto &rm : rootMoves) {
            auto move = rm[0];
            rootPos.doMove(move, si);

            WDLScore wdl{ -probeWDL(rootPos, state) };

            rootPos.undoMove(move);

            if (state == PS_FAILURE) {
                return false;
            }

            rm.tbRank = wdlToRank[wdl + 2];

            if (!move50Rule) {
                wdl =
                    wdl > WDL_DRAW ? WDL_WIN :
                    wdl < WDL_DRAW ? WDL_LOSS : WDL_DRAW;
            }
            rm.tbValue = wdlToValue[wdl + 2];
        }
        return true;
    }

    /// Use the DTZ tables to rank root moves.
    ///
    /// A return value false indicates that not all probes were successful.
    bool rootProbeDTZ(Position &rootPos, RootMoves &rootMoves) {
        assert(rootMoves.size() != 0);

        // Obtain 50-move counter for the root position
        auto clockPly{ rootPos.clockPly() };
        // Check whether a position was repeated since the last zeroing move.
        bool repeated{ rootPos.repeated() };

        i16 bound{ i16(Options["SyzygyMove50Rule"] ? 900 : 1) };
        i32 dtz;

        StateInfo si;
        ProbeState state;
        // Probe and rank each move
        for (auto &rm : rootMoves) {
            auto move{ rm[0] };
            rootPos.doMove(move, si);

            // Calculate dtz for the current move counting from the root position
            if (rootPos.clockPly() == 0) {
                // In case of a zeroing move, dtz is one of -101/-1/0/+1/+101
                dtz = beforeZeroingDTZ(-probeWDL(rootPos, state));
            }
            else {
                // Otherwise, take dtz for the new position and correct by 1 ply
                dtz = -probeDTZ(rootPos, state);
                dtz = dtz > 0 ? dtz + 1 :
                      dtz < 0 ? dtz - 1 : dtz;
            }
            // Make sure that a mating move is assigned a dtz value of 1
            if (rootPos.checkers() != 0
             && dtz == 2
             && MoveList<LEGAL>(rootPos).size() == 0) {
                dtz = 1;
            }

            rootPos.undoMove(move);

            if (state == PS_FAILURE) {
                return false;
            }

            // Better moves are ranked higher. Certain wins are ranked equally.
            // Losing moves are ranked equally unless a 50-move draw is in sight.
            i16 r{
                i16(dtz > 0 ? (+dtz     + clockPly < 100 && !repeated ? +1000 : +1000 - (clockPly + dtz)) :
                    dtz < 0 ? (-dtz * 2 + clockPly < 100              ? -1000 : -1000 + (clockPly - dtz)) : 0) };

            rm.tbRank = r;
            // Determine the score to be displayed for this move. Assign at least
            // 1 cp to cursed wins and let it grow to 49 cp as the positions gets
            // closer to a real win.
            rm.tbValue =
                r >= bound  ? +VALUE_MATE_1_MAX_PLY - 1 :
                r > 0       ? (VALUE_EG_PAWN * std::max(+3, r - 800)) / 200 :
                r == 0      ?  VALUE_DRAW :
                r > -bound  ? (VALUE_EG_PAWN * std::min(-3, r + 800)) / 200 :
                              -VALUE_MATE_1_MAX_PLY + 1;
        }
        return true;
    }

    void initialize(string const &paths) {
        static bool initialized = false;

        if (!initialized) {

            // MapB1H1H7[] encodes a square below a1-h8 diagonal to 0..27
            i32 code{ 0 };
            for (Square s = SQ_A1; s <= SQ_H8; ++s) {
                if (offA1H8(s) < 0) {
                    MapB1H1H7[s] = code++;
                }
            }
            // MapA1D1D4[] encodes a square in the a1-d1-d4 triangle to 0..9
            code = 0;
            vector<Square> diagonal;
            for (Square s : {
                    SQ_A1, SQ_B1, SQ_C1, SQ_D1,
                    SQ_A2, SQ_B2, SQ_C2, SQ_D2,
                    SQ_A3, SQ_B3, SQ_C3, SQ_D3,
                    SQ_A4, SQ_B4, SQ_C4, SQ_D4 }) {
                auto off = offA1H8(s);

                if (off < 0) {
                    MapA1D1D4[s] = code++;
                }
                else
                if (off == 0) {
                    diagonal.push_back(s);
                }
            }
            // Diagonal squares are encoded as last ones
            for (Square s : diagonal) {
                MapA1D1D4[s] = code++;
            }

            code = 0;
            // MapKK[] encodes all the 461 possible legal positions of two kings where the first is in the a1-d1-d4 triangle.
            // If the first king is on the a1-d4 diagonal, the other one shall not to be above the a1-h8 diagonal.
            vector<std::pair<i32, Square>> bothOnDiagonal;
            for (i16 idx = 0; idx < MapKKSize; ++idx) {
                for (Square s1 = SQ_A1; s1 <= SQ_D4; ++s1) {
                    if (idx == MapA1D1D4[s1]
                     && (idx != 0 || s1 == SQ_B1)) { // SQ_B1 is mapped to 0

                        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2) {
                            if (contains(PieceAttacksBB[KING][s1] | s1, s2)) {
                                continue; // Illegal position
                            }

                            auto off1 = offA1H8(s1);
                            auto off2 = offA1H8(s1);
                            if (off1 == 0 && off2 > 0) {
                                continue; // First on diagonal, second above
                            }

                            if (off1 == 0 && off2 == 0) {
                                bothOnDiagonal.emplace_back(idx, s2);
                            }
                            else {
                                MapKK[idx][s2] = code++;
                            }
                        }
                    }
                }
            }

            // Legal positions with both kings on diagonal are encoded as last ones
            for (auto pair : bothOnDiagonal) {
                MapKK[pair.first][pair.second] = code++;
            }

            // Binomial[] stores the Binomial Coefficients using Pascal rule. There
            // are Binomial[k][n] ways to choose k elements from a set of n elements.
            Binomial[0][0] = 1;
            for (i32 n = 1; n < SQUARES; ++n) // Squares
            {
                for (i32 k = 0; k <= TBPIECES - 2 && k <= n; ++k) // Pieces
                {
                    Binomial[k][n] = (k > 0 ? Binomial[k - 1][n - 1] : 0)
                                   + (k < n ? Binomial[k][n - 1] : 0);
                }
            }

            // MapPawns[s] encodes squares a2-h7 to 0..47. This is the number of possible
            // available squares when the leading one is in square. Moreover the pawn with
            // highest MapPawns[] is the leading pawn, the one nearest the edge and,
            // among pawns with same file, the one with lowest rank.
            i32 availableSq{ 47 }; // 63 - 16; // Available squares when lead pawn is in a2

            // Init the tables for the encoding of leading pawns group:
            // with 6-men TB can have up to 5 leading pawns (KPPPPPK).
            for (i32 lpCount = 1; lpCount <= TBPIECES - 2; ++lpCount) {
                for (File f = FILE_A; f <= FILE_D; ++f) {
                    // Restart the index at every file because TB table is splitted
                    // by file, so we can reuse the same index for different files.
                    i32 idx{ 0 };

                    // Sum all possible combinations for a given file, starting with
                    // the leading pawn on rank 2 and increasing the rank.
                    for (Rank r = RANK_2; r <= RANK_7; ++r) {
                        auto sq{ makeSquare(f, r) };

                        // Compute MapPawns[] at first pass.
                        // If sq is the leading pawn square, any other pawn cannot be
                        // below or more toward the edge of sq. There are 47 available
                        // squares when sq = a2 and reduced by 2 for any rank increase
                        // due to mirroring: sq == a3 -> no a2, h2, so MapPawns[a3] = 45
                        if (lpCount == 1) {
                            MapPawns[sq]           = availableSq--;
                            MapPawns[flipFile(sq)] = availableSq--; // Horizontal flip
                        }
                        LeadPawnIdx[lpCount][sq] = idx;
                        idx += Binomial[lpCount - 1][MapPawns[sq]];
                    }
                    // After a file is traversed, store the cumulated per-file index
                    LeadPawnsSize[lpCount][f] = idx;
                }
            }
            initialized = true;
        }

        TBTables.clear();
        MaxPieceLimit = 0;

        if (whiteSpaces(paths)) {
            return;
        }

        // Paths Example
        // (Windows)= D:\tb\wdl345;D:\tb\wdl6;D:\tb\dtz345;D:\tb\dtz6
        // (Unix-based OS)= .\tb\wdl345:.\tb\wdl6:.\tb\dtz345:.\tb\dtz6

#   if defined(_WIN32)
        constexpr char Delimiter{ ';' };
#   else
        constexpr char Delimiter{ ':' };
#   endif

        // Split paths by delimiter
        TBFile::Paths.clear();
        std::stringstream ss{ paths };
        string path;
        while (std::getline(ss, path, Delimiter)) {
            //replace(path, '\\', '/');
            //trim(path);
            //if (whiteSpaces(path)) {
            //    continue;
            //}
            TBFile::Paths.push_back(path);
        }

        for (PieceType p1 = PAWN; p1 <= QUEN; ++p1) {
            TBTables.add({ KING, p1, KING });

            for (PieceType p2 = PAWN; p2 <= p1; ++p2) {
                TBTables.add({ KING, p1, KING, p2 });
                TBTables.add({ KING, p1, p2, KING });

                for (PieceType p3 = PAWN; p3 <= QUEN; ++p3) {
                    TBTables.add({ KING, p1, p2, KING, p3 });
                }
                for (PieceType p3 = PAWN; p3 <= p2; ++p3) {
                    TBTables.add({ KING, p1, p2, p3, KING });

                    for (PieceType p4 = PAWN; p4 <= QUEN; ++p4) {
                        TBTables.add({ KING, p1, p2, p3, KING, p4 });

                        for (PieceType p5 = PAWN; p5 <= p4; ++p5) {
                            TBTables.add({ KING, p1, p2, p3, KING, p4, p5 });
                        }
                    }
                    for (PieceType p4 = PAWN; p4 <= p3; ++p4) {
                        TBTables.add({ KING, p1, p2, p3, p4, KING });

                        for (PieceType p5 = PAWN; p5 <= p4; ++p5) {
                            TBTables.add({ KING, p1, p2, p3, p4, p5, KING });
                        }
                        for (PieceType p5 = PAWN; p5 <= QUEN; ++p5) {
                            TBTables.add({ KING, p1, p2, p3, p4, KING, p5 });
                        }
                    }
                }
                for (PieceType p3 = PAWN; p3 <= p1; ++p3) {
                    for (PieceType p4 = PAWN; p4 <= (p3 < p1 ? p3 : p2); ++p4) {
                        TBTables.add({ KING, p1, p2, KING, p3, p4 });
                    }
                }
            }
        }

        sync_cout << "info string Tablebases found " << TBTables.size() << sync_endl;
    }
}

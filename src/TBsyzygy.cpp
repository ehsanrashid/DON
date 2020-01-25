#include "TBsyzygy.h"

#include <algorithm>
#include <cstdint>
#include <cstring>   // For std::memset and std::memcpy
#include <deque>
#include <fstream>
#include <sstream>
#include <iostream>
#include <list>
#include <vector>

#include "BitBoard.h"
#include "Engine.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "Option.h"
#include "Position.h"
#include "Thread.h"

using namespace std;

#if defined(_WIN32)
#   if !defined(NOMINMAX)
#       define NOMINMAX // Disable macros min() and max()
#   endif
#   if !defined(WIN32_LEAN_AND_MEAN)
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#   undef WIN32_LEAN_AND_MEAN
#   undef NOMINMAX

// Create a string with last error message
string GetLastErrorString()
{
    DWORD error = GetLastError();
    if (0 != error)
    {
        LPTSTR msg_buffer = nullptr;
        DWORD buf_len = FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
            LPTSTR (&msg_buffer),
            0,
            nullptr);
        if (0 != buf_len)
        {
            string message(msg_buffer, buf_len);
            LocalFree(msg_buffer);
            return message;
        }
    }
    return {""};
}

#else
#   include <fcntl.h>
#   include <unistd.h>
#   include <sys/mman.h>
#   include <sys/stat.h>
#endif

namespace TBSyzygy {

    using namespace BitBoard;

    string PathString = "";
    i32    MaxLimitPiece = 0;

    namespace {

        constexpr i32 TBPIECES = 6;

        // Type of table
        enum TBType : u08
        {
            KEY,
            WDL,
            DTZ
        };

        // Each table has a set of flags: all of them refer to DTZ tables, the last one to WDL tables
        enum TBFlag : u08
        {
            STM = 1,
            MAPPED = 2,
            WIN_PLIES = 4,
            LOSS_PLIES = 8,
            WIDE = 16,
            SINGLE_VALUE = 128
        };

        enum Endian : u08
        {
            BIG,
            LITTLE,
            UNKNOWN
        };

        constexpr i16 WDL_To_Rank[] =
        {
            -1000,
            -899,
            0,
            +899,
            +1000
        };
        constexpr Value WDL_To_Value[] =
        {
            -VALUE_MATE + (DEP_MAX + 1),
            VALUE_DRAW - 2,
            VALUE_DRAW,
            VALUE_DRAW + 2,
            +VALUE_MATE - (DEP_MAX + 1)
        };

        i32 MapPawns[SQ_NO];
        i32 MapB1H1H7[SQ_NO];
        i32 MapA1D1D4[SQ_NO];
        i32 MapKK[10][SQ_NO]; // [MapA1D1D4][SQ_NO]

        i32 Binomial[6][SQ_NO];    // [k][n] k elements from a set of n elements
        i32 LeadPawnIdx[5][SQ_NO]; // [lead_pawn_count][SQ_NO]
        i32 LeadPawnsSize[5][4];   // [lead_pawn_count][F_A..F_D]

        /// Comparison function to sort leading pawns in ascending MapPawns[] order
        bool pawns_comp(Square s1, Square s2)
        {
            return MapPawns[s1] < MapPawns[s2];
        }
        i32 off_A1H8(Square sq)
        {
            return i32(_rank(sq)) - i32(_file(sq));
        }

        template<typename T, i32 Half = sizeof (T) / 2, i32 End = sizeof (T) - 1>
        inline void swap_endian(T &x)
        {
            static_assert (std::is_unsigned<T>::value, "Argument of swap_endian not unsigned");

            u08 *c = (u08*)(&x);
            for (i32 i = 0; i < Half; ++i)
            {
                u08 tmp = c[i];
                c[i] = c[End - i];
                c[End - i] = tmp;
            }
        }
        template<>
        inline void swap_endian<u08>(u08&)
        {}

        template<typename T, Endian E>
        T number(void *addr)
        {
            static const union { u32 i; char c[4]; } U = { 0x01020304 };
            static const Endian End = (0x04 == U.c[0]) ? Endian::LITTLE :
                                      (0x01 == U.c[0]) ? Endian::BIG : Endian::UNKNOWN;
            assert(Endian::UNKNOWN != End);
            T v;
            if (0 != ((uintptr_t)(addr) & (alignof (T) - 1))) // Unaligned pointer (very rare)
            {
                std::memcpy(&v, addr, sizeof (T));
            }
            else
            {
                v = *((T*)(addr));
            }
            if (E != End)
            {
                swap_endian(v);
            }
            return v;
        }

        Piece tb_piece(i32 pc) { return 0 != pc ? Piece(pc - 1) : NO_PIECE; }

        // DTZ tables don't store valid scores for moves that reset the rule50 counter
        // like captures and pawn moves but we can easily recover the correct dtz of the
        // previous move if we know the position's WDL score.
        i32 dtz_before_zeroing(WDLScore wdl)
        {
            switch (wdl)
            {
            case WDLScore::LOSS:         return -1;
            case WDLScore::BLESSED_LOSS: return -101;
            case WDLScore::CURSED_WIN:   return +101;
            case WDLScore::WIN:          return +1;
            case WDLScore::DRAW:
            default:                     return 0;
            }
        }

        // Numbers in little endian used by sparse_index[] to point into block_length[]
        struct SparseEntry
        {
            char block[4];   // Number of block
            char offset[2];  // Offset within the block
        };

        static_assert (sizeof (SparseEntry) == 6, "SparseEntry size incorrect");

        typedef u16 Sym; // Huffman symbol

        struct LR
        {
        public:
            enum Side : u08
            {
                Left,
                Right,
                Center
            };

            // The 1st 12 bits is the left-hand symbol,
            // the 2nd 12 bits is the right-hand symbol.
            // If symbol has length 1, then the first byte is the stored value.
            u08 lr[3];

            template<Side S>
            Sym get()
            {
                return S == Side::Left   ? ((lr[1] & 0xF) << 8) | lr[0] :
                       S == Side::Right  ?  (lr[2] << 4) | (lr[1] >> 4) :
                       S == Side::Center ?   lr[0] : (assert(false), -1);
            }
        };

        static_assert (sizeof (LR) == 3, "LR size incorrect");

        class TBFile
            : public ifstream
        {
        private:
            string filename;

        public:
            // Look for and open the file among the Paths directories where the .rtbw and .rtbz files can be found.
            static vector<string> Paths;

            TBFile (const string &code, const string &ext)
            {
                auto file = code;
                file.insert(file.find('K', 1), "v");
                file += ext;
                for (const auto &path : Paths)
                {
                    const auto fname = append_path(path, file);
                    ifstream::open(fname);
                    if (ifstream::is_open())
                    {
                        filename = fname;
                        break;
                    }
                }
            }

            // Memory map the file and check it. File should be already open and will be
            // closed after mapping.
            u08* map(void **base_address, u64 *mapping, TBType type)
            {
                assert(!white_spaces(filename) && ifstream::is_open());

                ifstream::close(); // Need to re-open to get native file descriptor

#           if defined(_WIN32)
                HANDLE fd = CreateFile (
                                filename.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_RANDOM_ACCESS, // FILE_FLAG_RANDOM_ACCESS is only a hint to Windows and as such may get ignored.
                                nullptr);
                if (INVALID_HANDLE_VALUE == fd)
                {
                    *base_address = nullptr;
                    return nullptr;
                }

                DWORD high_size;
                DWORD low_size = GetFileSize (fd, &high_size);

                if (16 != low_size % 64)
                {
                    cerr << "Corrupt tablebase file " << filename << endl;
                    stop(EXIT_FAILURE);
                }

                HANDLE mmap = CreateFileMapping (
                                fd,
                                nullptr,
                                PAGE_READONLY,
                                high_size,
                                low_size,
                                nullptr);
                CloseHandle(fd);
                if (nullptr == mmap)
                {
                    cerr << "CreateFileMapping() failed, name = " << filename
                         << ", error = " << GetLastErrorString() << endl;
                    stop(EXIT_FAILURE);
                    return nullptr;
                }

                *mapping = u64(mmap);
                *base_address = MapViewOfFile (mmap, FILE_MAP_READ, 0, 0, 0);
                if (nullptr == *base_address)
                {
                    cerr << "MapViewOfFile() failed, name = " << filename << ", error = " << GetLastErrorString() << endl;
                    stop(EXIT_FAILURE);
                    return nullptr;
                }
#           else
                i32 fd = ::open(
                            filename.c_str(),
                            O_RDONLY);
                if (-1 == fd)
                {
                    *base_address = nullptr;
                    return nullptr;
                }

                struct stat statbuf;
                fstat (fd, &statbuf);
                if (0 == statbuf.st_size)
                {
                    cerr << "fstat() failed, name = " << filename << endl;
                    ::close(fd);
                    stop(EXIT_FAILURE);
                    return nullptr;
                }
                if (16 != statbuf.st_size % 64)
                {
                    cerr << "Corrupt tablebase file " << filename << endl;
                    ::close(fd);
                    stop(EXIT_FAILURE);
                    return nullptr;
                }

                *mapping = statbuf.st_size;
                *base_address = mmap(nullptr,
                                     statbuf.st_size,
                                     PROT_READ,
                                     MAP_SHARED,
                                     fd,
                                     0);
                madvise(*base_address, statbuf.st_size, MADV_RANDOM);
                ::close(fd);
                if (MAP_FAILED == *base_address)
                {
                    cerr << "Could not mmap() " << filename << endl;
                    stop(EXIT_FAILURE);
                }
#           endif

                constexpr u08 TB_MAGIC[][4] =
                {
                    { 0xD7, 0x66, 0x0C, 0xA5 },
                    { 0x71, 0xE8, 0x23, 0x5D }
                };

                u08 *data = (u08*)(*base_address);
                if (0 != std::memcmp(data, TB_MAGIC[(WDL == type)], 4))
                {
                    cerr << "Corrupted table in file " << filename << endl;
                    unmap(*base_address, *mapping);
                    *base_address = nullptr;
                    return nullptr;
                }

                return data + 4; // Skip Magics's header
            }

            static void unmap(void *base_address, u64 mapping)
            {
#           if defined(_WIN32)
                UnmapViewOfFile (base_address);
                CloseHandle(HANDLE(mapping));
#           else
                munmap(base_address, mapping);
#           endif
            }
        };

        vector<string> TBFile::Paths;

        struct PairsData
        {
        public:
            i32 flags;
            size_t block_size;              // Block size in bytes
            size_t span;                    // About every span values there is a SparseIndex[] entry
            i32 num_blocks;                 // Number of blocks in the TB file
            i32 max_sym_len;                // Maximum length in bits of the Huffman symbols
            i32 min_sym_len;                // Minimum length in bits of the Huffman symbols
            Sym *lowest_sym;                // lowest_sym[l] is the symbol of length l with the lowest value
            LR *btree;                      // btree[sym] stores the left and right symbols that expand sym
            u16 *block_length;              // Number of stored positions (minus one) for each block: 1..65536
            i32 block_length_size;          // Size of block_length[] table: padded so it's bigger than num_blocks
            SparseEntry *sparse_index;      // Partial indices into block_length[]
            size_t sparse_index_size;       // Size of sparse_index[] table
            u08 *data;                      // Start of Huffman compressed data
            vector<u64> base64;             // base64[l - min_sym_len] is the 64bit-padded lowest symbol of length l
            vector<u08> sym_len;            // Number of values (-1) represented by a given Huffman symbol: 1..256
            Piece pieces[TBPIECES];         // Position pieces: the order of pieces defines the groups
            u64 group_idx[TBPIECES+1];      // Start index used for the encoding of the group's pieces
            i32 group_len[TBPIECES+1];      // Number of pieces in a given group: KRKN ->(3, 1)
            u16 map_idx[4];                 // WDLWin, WDLLoss, WDLCursedWin, WDLBlessedLoss (used in DTZ)
        };

        template<TBType Type>
        struct TBTable
        {
            typedef typename conditional<WDL == Type, WDLScore, i32>::type Ret;

            static constexpr i32 Sides = WDL == Type ? 2 : 1;

            atomic<bool> ready;
            void *base_address;
            u08 *map;
            u64 mapping;
            Key key1;
            Key key2;
            i32 piece_count;
            bool has_pawns;
            bool has_unique_pieces;
            u08 pawn_count[CLR_NO]; // [Lead color / other color]
            PairsData items[Sides][4]; // [wtm / btm][FILE_A..FILE_D or 0]

            PairsData* get(i32 stm, i32 f)
            {
                return &items[stm % Sides][has_pawns ? f : 0];
            }

            TBTable()
                : ready(false)
                , base_address(nullptr)
                , map(nullptr)
                , mapping(0)
            {}

            explicit TBTable(const string&);
            explicit TBTable(const TBTable<WDL>&);

            virtual ~TBTable()
            {
                if (nullptr != base_address)
                {
                    TBFile::unmap(base_address, mapping);
                }
            }
        };

        template<>
        TBTable<WDL>::TBTable(const string &code)
            : TBTable()
        {
            StateInfo si;
            Position pos;
            key1 = pos.setup(code, WHITE, si).si->matl_key;
            piece_count = pos.count();
            has_pawns = 0 != pos.count(PAWN);
            has_unique_pieces = false;
            for (auto pc : { W_PAWN, W_NIHT, W_BSHP, W_ROOK, W_QUEN,
                             B_PAWN, B_NIHT, B_BSHP, B_ROOK, B_QUEN })
            {
                if (1 == pos.count(pc))
                {
                    has_unique_pieces = true;
                    break;
                }
            }

            // Set the leading color. In case both sides have pawns the leading color
            // is the side with less pawns because this leads to better compression.
            auto lead_color = pos.count(BLACK|PAWN) == 0
                           || (   pos.count(WHITE|PAWN) != 0
                               && pos.count(BLACK|PAWN) >= pos.count(WHITE|PAWN)) ? WHITE : BLACK;

            pawn_count[0] = u08(pos.count( lead_color|PAWN));
            pawn_count[1] = u08(pos.count(~lead_color|PAWN));

            key2 = pos.setup(code, BLACK, si).si->matl_key;
        }

        template<>
        TBTable<DTZ>::TBTable(const TBTable<WDL> &wdl)
            : TBTable()
        {
            key1 = wdl.key1;
            key2 = wdl.key2;
            piece_count = wdl.piece_count;
            has_pawns = wdl.has_pawns;
            has_unique_pieces = wdl.has_unique_pieces;
            pawn_count[0] = wdl.pawn_count[0];
            pawn_count[1] = wdl.pawn_count[1];
        }

        class TBTables
        {
        private:
            static constexpr i32 Size = 1 << 12; // 4K table, indexed by key's 12 lsb
            static constexpr i32 Overflow = 1;   // Number of elements allowed to map to the last bucket

            typedef tuple<Key, TBTable<WDL>*, TBTable<DTZ>*> Entry;

            Entry entries[Size + Overflow];

            deque<TBTable<WDL>> wdl_table;
            deque<TBTable<DTZ>> dtz_table;

            void insert(Key key, TBTable<WDL> *wdl, TBTable<DTZ> *dtz)
            {
                u32 homeBucket = (u32)key & (Size - 1);
                Entry entry = std::make_tuple(key, wdl, dtz);

                // Ensure last element is empty to avoid overflow when looking up
                for (u32 bucket = homeBucket; bucket < Size + Overflow - 1; ++bucket)
                {
                    Key otherKey = std::get<KEY>(entries[bucket]);
                    if (otherKey == key || !std::get<WDL>(entries[bucket]))
                    {
                        entries[bucket] = entry;
                        return;
                    }

                    // Robin Hood hashing: If we've probed for longer than this element,
                    // insert here and search for a new spot for the other element instead.
                    u32 otherHomeBucket = (u32)otherKey & (Size - 1);
                    if (otherHomeBucket > homeBucket)
                    {
                        swap(entry, entries[bucket]);
                        key = otherKey;
                        homeBucket = otherHomeBucket;
                    }
                }

                cerr << "HSHMAX too low!" << endl;
                stop(EXIT_FAILURE);
            }

        public:

            template<TBType Type>
            TBTable<Type>* get(Key key)
            {
                for (const auto *entry = &entries[(u32)key & (Size - 1)];
                     entry - entries < Size;
                     ++entry)
                {
                    if (   std::get<KEY>(*entry) == key
                        || !std::get<Type>(*entry))
                    {
                        return std::get<Type>(*entry);
                    }
                }
                return nullptr;
            }

            void clear()
            {
                std::memset(entries, 0, sizeof (entries));
                wdl_table.clear();
                dtz_table.clear();
            }

            size_t size() const
            {
                return wdl_table.size();
            }

            void add(const vector<PieceType> &pieces)
            {
                string code;
                for (auto pt : pieces)
                {
                    code += PieceChar[pt];
                }

                TBFile file(code, ".rtbw");
                if (!file.is_open()) // Only WDL file is checked
                {
                    return;
                }
                file.close();

                MaxLimitPiece = std::max(i32(pieces.size()), MaxLimitPiece);

                wdl_table.emplace_back(code);
                dtz_table.emplace_back(wdl_table.back());
                // Insert into the hash keys for both colors: KRvK with KR white and black
                insert(wdl_table.back().key1, &wdl_table.back(), &dtz_table.back());
                insert(wdl_table.back().key2, &wdl_table.back(), &dtz_table.back());
            }
        };

        TBTables TB_Tables;

        /// TB tables are compressed with canonical Huffman code. The compressed data is divided into
        /// blocks of size d->block_size, and each block stores a variable number of symbols.
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
        i32 decompress_pairs(PairsData *d, u64 idx)
        {
            // Special case where all table positions store the same value
            if (0 != (d->flags & TBFlag::SINGLE_VALUE))
            {
                return d->min_sym_len;
            }

            // First we need to locate the right block that stores the value at index "idx".
            // Because each block n stores block_length[n] + 1 values, the index i of the block
            // that contains the value at position idx is:
            //
            //     for (i = -1, sum = 0; sum <= idx; ++i)
            //         sum += block_length[i + 1] + 1;
            //
            // This can be slow, so we use SparseIndex[] populated with a set of SparseEntry that
            // point to known indices into block_length[]. Namely SparseIndex[k] is a SparseEntry
            // that stores the block_length[] index and the offset within that block of the value
            // with index I(k), where:
            //
            //     I(k) = k * d->span + d->span / 2      (1)

            // First step is to get the 'k' of the I(k) nearest to our idx, using definition (1)
            u32 k = idx / d->span;

            // Then we read the corresponding SparseIndex[] entry
            u32 block  = number<u32, Endian::LITTLE>(&d->sparse_index[k].block);
            i32 offset = number<u16, Endian::LITTLE>(&d->sparse_index[k].offset);

            // Now compute the difference idx - I(k). From definition of k we know that
            //
            //     idx = k * d->span + idx % d->span    (2)
            //
            // So from (1) and (2) we can compute idx - I(K):
            i32 diff = idx % d->span - d->span / 2;

            // Sum the above to offset to find the offset corresponding to our idx
            offset += diff;

            // Move to previous/next block, until we reach the correct block that contains idx,
            // that is when 0 <= offset <= d->block_length[block]
            while (offset < 0)
            {
                offset += d->block_length[--block] + 1;
            }

            while (offset > d->block_length[block])
            {
                offset -= d->block_length[block++] + 1;
            }

            // Finally, we find the start address of our block of canonical Huffman symbols
            u32 *ptr = (u32*)(d->data + ((u64)block * d->block_size));

            // Read the first 64 bits in our block, this is a (truncated) sequence of
            // unknown number of symbols of unknown length but we know the first one
            // is at the beginning of this 64 bits sequence.
            u64 buf64 = number<u64, Endian::BIG>(ptr);
            ptr += 2;
            i32 buf64_size = 64;
            Sym sym;

            do
            {
                i32 len = 0; // This is the symbol length - d->min_sym_len

                // Now get the symbol length. For any symbol s64 of length l right-padded
                // to 64 bits we know that d->base64[l-1] >= s64 >= d->base64[l] so we
                // can find the symbol length iterating through base64[].
                while (buf64 < d->base64[len])
                {
                    ++len;
                }

                // All the symbols of a given length are consecutive integers (numerical
                // sequence property), so we can compute the offset of our symbol of
                // length len, stored at the beginning of buf64.
                sym = Sym((buf64 - d->base64[len]) >> (64 - len - d->min_sym_len));

                // Now add the value of the lowest symbol of length len to get our symbol
                sym += number<Sym, Endian::LITTLE>(&d->lowest_sym[len]);

                // If our offset is within the number of values represented by symbol sym
                if (offset < d->sym_len[sym] + 1)
                {
                    break;
                }

                // ...otherwise update the offset and continue to iterate
                offset -= d->sym_len[sym] + 1;
                len += d->min_sym_len;  // Get the real length
                buf64 <<= len;          // Consume the just processed symbol
                buf64_size -= len;

                if (buf64_size <= 32)
                {
                    // Refill the buffer
                    buf64_size += 32;
                    buf64 |= u64(number<u32, Endian::BIG>(ptr++)) << (64 - buf64_size);
                }
            } while (true);

            // Ok, now we have our symbol that expands into d->sym_len[sym] + 1 symbols.
            // We binary-search for our value recursively expanding into the left and
            // right child symbols until we reach a leaf node where sym_len[sym] + 1 == 1
            // that will store the value we need.
            while (0 != d->sym_len[sym])
            {
                Sym left = d->btree[sym].get<LR::Side::Left>();

                // If a symbol contains 36 sub-symbols (d->sym_len[sym] + 1 = 36) and
                // expands in a pair (d->sym_len[left] = 23, d->sym_len[right] = 11), then
                // we know that, for instance the ten-th value (offset = 10) will be on
                // the left side because in Recursive Pairing child symbols are adjacent.
                if (offset < d->sym_len[left] + 1)
                {
                    sym = left;
                }
                else
                {
                    offset -= d->sym_len[left] + 1;
                    sym = d->btree[sym].get<LR::Right>();
                }
            }

            return d->btree[sym].get<LR::Side::Center>();
        }

        bool check_dtz_stm(TBTable<WDL>*, Color, File)
        {
            return true;
        }

        bool check_dtz_stm(TBTable<DTZ> *entry, Color stm, File f)
        {
            return (entry->get(stm, f)->flags & TBFlag::STM) == stm
                || (   entry->key1 == entry->key2
                    && !entry->has_pawns);
        }

        /// DTZ scores are sorted by frequency of occurrence and then assigned the
        /// values 0, 1, 2, ... in order of decreasing frequency. This is done for each
        //// of the four WDLScore values. The mapping information necessary to reconstruct
        /// the original values is stored in the TB file and read during map[] init.
        WDLScore map_score(TBTable<WDL>*, File, i32 value, WDLScore)
        {
            return WDLScore(value - 2);
        }

        i32 map_score(TBTable<DTZ> *entry, File f, i32 value, WDLScore wdl)
        {
            constexpr i32 WDLMap[] = { 1, 3, 0, 2, 0 };

            i32 flags = entry->get(0, f)->flags;
            u08 *map = entry->map;
            u16* idx = entry->get(0, f)->map_idx;
            if (0 != (flags & TBFlag::MAPPED))
            {
                if (0 != (flags & TBFlag::WIDE))
                {
                    value = ((u16*)map)[idx[WDLMap[wdl + 2]] + value];
                }
                else
                {
                    value = map[idx[WDLMap[wdl + 2]] + value];
                }
            }

            // DTZ tables store distance to zero in number of moves or plies. We
            // want to return plies, so we have convert to plies when needed.
            if (   (WDLScore::WIN  == wdl && 0 == (flags & TBFlag::WIN_PLIES))
                || (WDLScore::LOSS == wdl && 0 == (flags & TBFlag::LOSS_PLIES))
                ||  WDLScore::CURSED_WIN == wdl
                ||  WDLScore::BLESSED_LOSS == wdl)
            {
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
        Ret do_probe_table(const Position &pos, T *entry, WDLScore wdl, ProbeState &state)
        {
            Square squares[TBPIECES];
            Piece pieces[TBPIECES];
            i32 size = 0;

            Bitboard b;
            Bitboard lead_pawns;
            File tb_file;

            bool flip =
                // Black Symmetric
                // A given TB entry like KRK has associated two material keys: KRvK and KvKR.
                // If both sides have the same pieces keys are equal. In this case TB tables
                // only store the 'white to move' case, so if the position to lookup has black
                // to move, we need to switch the color and flip the squares before to lookup.
                        (   pos.active == BLACK
                         && entry->key1 == entry->key2)
                // Black Stronger
                // TB files are calculated for white as stronger side. For instance we have
                // KRvK, not KvKR. A position where stronger side is white will have its
                // material key == entry->key1, otherwise we have to switch the color and
                // flip the squares before to lookup.
                     || (pos.si->matl_key != entry->key1);

            auto stm = flip ? ~pos.active : pos.active;

            // For pawns, TB files store 4 separate tables according if leading pawn is on
            // file a, b, c or d after reordering. The leading pawn is the one with maximum
            // MapPawns[] value, that is the one most toward the edges and with lowest rank.
            if (entry->has_pawns)
            {
                // In all the 4 tables, pawns are at the beginning of the piece sequence and
                // their color is the reference one. So we just pick the first one.
                Piece pc = flip ?
                            ~Piece(entry->get(0, 0)->pieces[0]) :
                             Piece(entry->get(0, 0)->pieces[0]);

                assert(PAWN == ptype(pc));

                lead_pawns = pos.pieces(pc);

                b = lead_pawns;
                do
                {
                    squares[size++] = flip ?
                                     ~pop_lsq(b) :
                                      pop_lsq(b);
                } while (0 != b);

                std::swap(squares[0], *std::max_element(squares, squares + size, pawns_comp));

                tb_file = _file(squares[0]);
                if (tb_file > F_D)
                {
                    tb_file = _file(!squares[0]); // Horizontal flip: SQ_H1 -> SQ_A1
                }
            }
            else
            {
                lead_pawns = 0;
                tb_file = F_A;
            }

            i32 lead_pawn_count = size;

            // DTZ tables are one-sided, i.e. they store positions only for white to
            // move or only for black to move, so check for side to move to be color,
            // early exit otherwise.
            if (!check_dtz_stm(entry, stm, tb_file))
            {
                state = ProbeState::CHANGE_STM;
                return Ret();
            }

            // Now we are ready to get all the position pieces(but the lead pawns) and
            // directly map them to the correct color and square.
            b = pos.pieces() ^ lead_pawns;
            do
            {
                auto s = pop_lsq(b);
                squares[size] = flip ?
                                 ~s :
                                  s;
                pieces[size] =  flip ?
                                 ~pos[s] :
                                  pos[s];
                ++size;
            } while (0 != b);

            assert(size >= 2);

            auto *d = entry->get(stm, tb_file);

            // Then we reorder the pieces to have the same sequence as the one stored
            // in pieces[i]: the sequence that ensures the best compression.
            for (i32 i = lead_pawn_count; i < size; ++i)
            {
                for (i32 j = i; j < size; ++j)
                {
                    if (d->pieces[i] == pieces[j])
                    {
                        std::swap(pieces[i], pieces[j]);
                        std::swap(squares[i], squares[j]);
                        break;
                    }
                }
            }

            // Now we map again the squares so that the square of the lead piece is in
            // the triangle A1-D1-D4.
            if (_file(squares[0]) > F_D)
            {
                for (i32 i = 0; i < size; ++i)
                {
                    squares[i] = !squares[i];
                }
            }

            u64 idx;
            // Encode leading pawns starting with the one with minimum MapPawns[] and
            // proceeding in ascending order.
            if (entry->has_pawns)
            {
                idx = LeadPawnIdx[lead_pawn_count][squares[0]];

                std::sort(squares + 1, squares + lead_pawn_count, pawns_comp);

                for (i32 i = 1; i < lead_pawn_count; ++i)
                {
                    idx += Binomial[i][MapPawns[squares[i]]];
                }
            }
            // In positions without pawns:
            else
            {
                // Flip the squares to ensure leading piece is below R_5.
                if (_rank(squares[0]) > R_4)
                {
                    for (i32 i = 0; i < size; ++i)
                    {
                        squares[i] = ~squares[i];
                    }
                }
                // Look for the first piece of the leading group not on the A1-D4 diagonal
                // and ensure it is mapped below the diagonal.
                for (i32 i = 0; i < d->group_len[0]; ++i)
                {
                    if (!off_A1H8(squares[i]))
                    {
                        continue;
                    }

                    if (off_A1H8(squares[i]) > 0) // A1-H8 diagonal flip: SQ_A3 -> SQ_C1
                    {
                        for (i32 j = i; j < size; ++j)
                        {
                            squares[j] = File(_rank(squares[j])) | Rank(_file(squares[j]));
                        }
                    }
                    break;
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
                if (entry->has_unique_pieces)
                {
                    i32 adjust1 = (squares[1] > squares[0]);
                    i32 adjust2 = (squares[2] > squares[0])
                                + (squares[2] > squares[1]);
                    // First piece is below a1-h8 diagonal. MapA1D1D4[] maps the b1-d1-d3
                    // triangle to 0...5. There are 63 squares for second piece and and 62
                    // (mapped to 0...61) for the third.
                    if (off_A1H8(squares[0]))
                    {
                        idx = (  MapA1D1D4[squares[0]] * 63
                               + (squares[1] - adjust1)) * 62
                            + (squares[2] - adjust2);
                    }
                    // First piece is on a1-h8 diagonal, second below: map this occurrence to
                    // 6 to differentiate from the above case, rank() maps a1-d4 diagonal
                    // to 0...3 and finally MapB1H1H7[] maps the b1-h1-h7 triangle to 0..27.
                    else
                    if (off_A1H8(squares[1]))
                    {
                        idx = (  6 * 63
                               + _rank(squares[0]) * 28
                               + MapB1H1H7[squares[1]]) * 62
                            + (squares[2] - adjust2);
                    }
                    // First two pieces are on a1-h8 diagonal, third below
                    else
                    if (off_A1H8(squares[2]))
                    {
                        idx = 6 * 63 * 62
                            + 4 * 28 * 62
                            +  _rank(squares[0]) * 7 * 28
                            + (_rank(squares[1]) - adjust1) * 28
                            +  MapB1H1H7[squares[2]];
                    }
                    // All 3 pieces on the diagonal a1-h8
                    else
                    {
                        idx = 6 * 63 * 62
                            + 4 * 28 * 62
                            + 4 *  7 * 28
                            +  _rank(squares[0]) * 7 * 6
                            + (_rank(squares[1]) - adjust1) * 6
                            + (_rank(squares[2]) - adjust2);
                    }
                }
                else
                {
                    // We don't have at least 3 unique pieces, like in KRRvKBB, just map the kings.
                    idx = MapKK[MapA1D1D4[squares[0]]][squares[1]];
                }
            }

            idx *= d->group_idx[0];
            auto *group_sq = squares + d->group_len[0];

            // Encode remaining pawns then pieces according to square, in ascending order
            bool pawn_remain = entry->has_pawns
                            && 0 != entry->pawn_count[1];

            i32 group_idx = 0;
            while (0 != d->group_len[++group_idx])
            {
                assert(0 <= d->group_len[group_idx] && d->group_len[group_idx] <= 6);
                std::sort (group_sq, group_sq + d->group_len[group_idx]);
                u64 n = 0;

                // Map down a square if "comes later" than a square in the previous
                // groups (similar to what done earlier for leading group pieces).
                for (i32 i = 0; i < d->group_len[group_idx]; ++i)
                {
                    auto adjust = std::count_if (squares, group_sq,
                                                 [&](Square s)
                                                 {
                                                     return group_sq[i] > s;
                                                 });
                    n += Binomial[i + 1][group_sq[i] - adjust - 8 * (pawn_remain)];
                }

                pawn_remain = false;
                idx += n * d->group_idx[group_idx];
                group_sq += d->group_len[group_idx];
            }

            // Now that we have the index, decompress the pair and get the score
            return map_score(entry, tb_file, decompress_pairs(d, idx), wdl);
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
        void set_groups(T &e, PairsData *d, i32 *order, File f)
        {
            i32 first_len = e.has_pawns ?
                                0 :
                                e.has_unique_pieces ?
                                    3 : 2;
            i32 n = 0;
            d->group_len[n] = 1;

            // Number of pieces per group is stored in group_len[], for instance in KRKN
            // the encoder will default on '111', so group_len[] will be (3, 1).
            for (i32 i = 1; i < e.piece_count; ++i)
            {
                if (   --first_len > 0
                    || d->pieces[i] == d->pieces[i - 1])
                {
                    d->group_len[n]++;
                }
                else
                {
                    d->group_len[++n] = 1;
                }
            }
            d->group_len[++n] = 0; // Zero-terminated

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
            bool pp = e.has_pawns
                   && 0 != e.pawn_count[1]; // Pawns on both sides
            i32 next = pp ? 2 : 1;
            i32 free_squares = 64 - d->group_len[0] - (pp ? d->group_len[1] : 0);
            u64 idx = 1;

            for (i32 k = 0; next < n || k == order[0] || k == order[1]; ++k)
            {
                if (k == order[0]) // Leading pawns or pieces
                {
                    d->group_idx[0] = idx;
                    idx *= e.has_pawns ?
                            LeadPawnsSize[d->group_len[0]][f] :
                            e.has_unique_pieces ?
                                31332 :
                                462;
                }
                else
                if (k == order[1]) // Remaining pawns
                {
                    d->group_idx[1] = idx;
                    idx *= Binomial[d->group_len[1]][48 - d->group_len[0]];
                }
                else // Remaining pieces
                {
                    d->group_idx[next] = idx;
                    idx *= Binomial[d->group_len[next]][free_squares];
                    free_squares -= d->group_len[next++];
                }
            }
            d->group_idx[n] = idx;
        }

        /// In Recursive Pairing each symbol represents a pair of children symbols. So
        /// read d->btree[] symbols data and expand each one in his left and right child
        /// symbol until reaching the leafs that represent the symbol value.
        u08 set_symlen(PairsData *d, Sym s, vector<bool> &visited)
        {
            visited[s] = true; // We can set it now because tree is acyclic
            Sym sr = d->btree[s].get<LR::Side::Right>();
            if (sr == 0xFFF)
            {
                return 0;
            }

            Sym sl = d->btree[s].get<LR::Side::Left>();

            if (!visited[sl])
            {
                d->sym_len[sl] = set_symlen(d, sl, visited);
            }
            if (!visited[sr])
            {
                d->sym_len[sr] = set_symlen(d, sr, visited);
            }

            return d->sym_len[sl] + d->sym_len[sr] + 1;
        }

        u08* set_sizes(PairsData *d, u08 *data)
        {
            d->flags = *data++;

            if (0 != (d->flags & TBFlag::SINGLE_VALUE))
            {
                d->num_blocks = 0;
                d->span = 0;
                d->block_length_size = 0;
                d->sparse_index_size = 0;
                d->min_sym_len = *data++; // Here we store the single value
                return data;
            }

            // group_len[] is a zero-terminated list of group lengths, the last group_idx[]
            // element stores the biggest index that is the tb size.
            u64 tb_size = d->group_idx[std::find(d->group_len, d->group_len + 7, 0) - d->group_len];

            d->block_size = u64(1) << *data++;
            d->span = u64(1) << *data++;
            d->sparse_index_size = (tb_size + d->span - 1) / d->span; // Round up
            i32 padding = number<u08, Endian::LITTLE>(data++);
            d->num_blocks = number<u32, Endian::LITTLE>(data); data += sizeof (u32);
            d->block_length_size = d->num_blocks + padding; // Padded to ensure SparseIndex[] does not point out of range.
            d->max_sym_len = *data++;
            d->min_sym_len = *data++;
            d->lowest_sym = (Sym*)(data);
            d->base64.resize(d->max_sym_len - d->min_sym_len + 1);

            // The canonical code is ordered such that longer symbols (in terms of
            // the number of bits of their Huffman code) have lower numeric value,
            // so that d->lowest_sym[i] >= d->lowest_sym[i+1] (when read as LittleEndian).
            // Starting from this we compute a base64[] table indexed by symbol length
            // and containing 64 bit values so that d->base64[i] >= d->base64[i+1].
            // See http://www.eecs.harvard.edu/~michaelm/E210/huffman.pdf
            for (i32 i = i32(d->base64.size()) - 2; i >= 0; --i)
            {
                d->base64[i] = (  d->base64[i + 1]
                                + number<Sym, Endian::LITTLE>(&d->lowest_sym[i])
                                - number<Sym, Endian::LITTLE>(&d->lowest_sym[i + 1])) / 2;

                assert(d->base64[i] * 2 >= d->base64[i+1]);
            }

            // Now left-shift by an amount so that d->base64[i] gets shifted 1 bit more
            // than d->base64[i+1] and given the above assert condition, we ensure that
            // d->base64[i] >= d->base64[i+1]. Moreover for any symbol s64 of length i
            // and right-padded to 64 bits holds d->base64[i-1] >= s64 >= d->base64[i].
            for (size_t i = 0; i < d->base64.size(); ++i)
            {
                d->base64[i] <<= 64 - i - d->min_sym_len; // Right-padding to 64 bits
            }
            data += d->base64.size() * sizeof (Sym);
            d->sym_len.resize(number<u16, Endian::LITTLE>(data)); data += sizeof (u16);
            d->btree = (LR*)(data);

            // The compression scheme used is "Recursive Pairing", that replaces the most
            // frequent adjacent pair of symbols in the source message by a new symbol,
            // reevaluating the frequencies of all of the symbol pairs with respect to
            // the extended alphabet, and then repeating the process.
            // See http://www.larsson.dogma.net/dcc99.pdf
            vector<bool> visited (d->sym_len.size());
            for (Sym sym = 0; sym < d->sym_len.size(); ++sym)
            {
                if (!visited[sym])
                {
                    d->sym_len[sym] = set_symlen(d, sym, visited);
                }
            }
            return data + d->sym_len.size() * sizeof (LR) + (d->sym_len.size() & 1);
        }

        u08* set_dtz_map(TBTable<WDL>&, u08*, File)
        {
            return nullptr;
        }

        u08* set_dtz_map(TBTable<DTZ> &e, u08 *data, File max_file)
        {
            e.map = data;
            for (auto f = F_A; f <= max_file; ++f)
            {
                auto flags = e.get(0, f)->flags;

                if (0 != (flags & TBFlag::MAPPED))
                {
                    if (0 != (flags & TBFlag::WIDE))
                    {
                        data += (uintptr_t)data & 1;  // Word alignment, we may have a mixed table
                        for (int i = 0; i < 4; ++i)
                        {   // Sequence like 3,x,x,x,1,x,0,2,x,x
                            e.get(0, f)->map_idx[i] = u16((u16*)data - (u16*)e.map + 1);
                            data += 2 * number<uint16_t, Endian::LITTLE>(data) + 2;
                        }
                    }
                    else
                    {
                        for (i32 i = 0; i < 4; ++i)
                        {
                            e.get(0, f)->map_idx[i] = u16(data - e.map + 1);
                            data += *data + 1;
                        }
                    }
                }
            }
            return data += (uintptr_t)data & 1; // Word alignment
        }

        template<typename T>
        void set(T &e, u08 *data)
        {
            assert(e.has_pawns        == !!(*data & 2)); // HasPawns
            assert((e.key1 != e.key2) == !!(*data & 1)); // Split

            data++; // First byte stores flags

            const i32  Sides = 2 == T::Sides && (e.key1 != e.key2) ? 2 : 1;
            const File MaxFile = e.has_pawns ? F_D : F_A;

            bool pp = e.has_pawns
                   && 0 != e.pawn_count[1]; // Pawns on both sides

            assert(!pp || 0 != e.pawn_count[0]);

            for (auto f = F_A; f <= MaxFile; ++f)
            {
                for (i32 i = 0; i < Sides; ++i)
                {
                    *e.get(i, f) = PairsData ();
                }

                i32 order[][2] =
                {
                    { *data & 0xF, pp ? *(data + 1) & 0xF : 0xF },
                    { *data >>  4, pp ? *(data + 1) >>  4 : 0xF }
                };

                data += 1 + (pp);

                for (i32 k = 0; k < e.piece_count; ++k, ++data)
                {
                    for (i32 i = 0; i < Sides; ++i)
                    {
                        e.get(i, f)->pieces[k] = tb_piece(i ? *data >> 4 : *data & 0xF);
                    }
                }

                for (i32 i = 0; i < Sides; ++i)
                {
                    set_groups(e, e.get(i, f), order[i], f);
                }
            }

            data += (uintptr_t)data & 1; // Word alignment

            for (auto f = F_A; f <= MaxFile; ++f)
            {
                for (i32 i = 0; i < Sides; ++i)
                {
                    data = set_sizes(e.get(i, f), data);
                }
            }

            data = set_dtz_map(e, data, MaxFile);

            PairsData *d;
            for (auto f = F_A; f <= MaxFile; ++f)
            {
                for (i32 i = 0; i < Sides; ++i)
                {
                    (d = e.get(i, f))->sparse_index = (SparseEntry*)(data);
                    data += d->sparse_index_size * sizeof (SparseEntry);
                }
            }
            for (auto f = F_A; f <= MaxFile; ++f)
            {
                for (i32 i = 0; i < Sides; ++i)
                {
                    (d = e.get(i, f))->block_length = (u16*)(data);
                    data += d->block_length_size * sizeof (u16);
                }
            }
            for (auto f = F_A; f <= MaxFile; ++f)
            {
                for (i32 i = 0; i < Sides; ++i)
                {
                    data = (u08*)(((uintptr_t)data + 0x3F) & ~0x3F); // 64 byte alignment
                    (d = e.get(i, f))->data = data;
                    data += d->num_blocks * d->block_size;
                }
            }
        }

        template<TBType Type>
        void* mapped(TBTable<Type> &e, const Position &pos)
        {
            static mutex mtx;

            // Use 'acquire' to avoid a thread reading 'ready' == true while
            // another is still working. (compiler reordering may cause this).
            if (e.ready.load(std::memory_order::memory_order_acquire))
            {
                return e.base_address; // Could be nullptr if file does not exist
            }

            unique_lock<mutex> lock(mtx);

            if (e.ready.load(std::memory_order::memory_order_relaxed)) // Recheck under lock
            {
                return e.base_address;
            }

            // Pieces strings in decreasing order for each color, like ("KPP","KR")
            string w, b;
            for (auto pt : { KING, QUEN, ROOK, BSHP, NIHT, PAWN })
            {
                w += string(pos.count(WHITE|pt), PieceChar[pt]);
                b += string(pos.count(BLACK|pt), PieceChar[pt]);
            }

            string code = e.key1 == pos.si->matl_key ?
                            w + b :
                            b + w;
            TBFile file (code, WDL == Type ? ".rtbw" : ".rtbz");
            u08 *data = file.map(&e.base_address, &e.mapping, Type);
            if (nullptr != data)
            {
                set(e, data);
            }
            e.ready.store(true, std::memory_order::memory_order_release);
            return e.base_address;
        }

        template<TBType Type, typename Ret = typename TBTable<Type>::Ret>
        Ret probe_table(const Position &pos, ProbeState &state, WDLScore wdl = WDLScore::DRAW)
        {
            if (0 == (pos.pieces() ^ pos.pieces(KING)))
            {
                return Ret(WDLScore::DRAW); // KvK
            }

            auto *entry = TB_Tables.get<Type>(pos.si->matl_key);

            if (   nullptr == entry
                || nullptr == mapped(*entry, pos))
            {
                state = ProbeState::FAILURE;
                return Ret();
            }

            return do_probe_table(pos, entry, wdl, state);
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
        /// the state to ZEROING_BEST_MOVE.
        WDLScore search(Position &pos, ProbeState &state, bool check_zeroing)
        {
            auto best_wdl = WDLScore::LOSS;

            StateInfo si;
            auto move_list = MoveList<GenType::LEGAL>(pos);
            size_t move_count = 0;
            for (auto &move : move_list)
            {
                if (   !pos.capture(move)
                    && (   !check_zeroing
                        || PAWN != ptype(pos[org_sq(move)])))
                {
                    continue;
                }

                ++move_count;

                pos.do_move(move, si);
                auto wdl = -search(pos, state, false);
                pos.undo_move(move);

                if (ProbeState::FAILURE == state)
                {
                    return WDLScore::DRAW;
                }

                if (best_wdl < wdl)
                {
                    best_wdl = wdl;

                    if (wdl >= WDLScore::WIN)
                    {
                        state = ProbeState::ZEROING_BEST_MOVE; // Winning DTZ-zeroing move
                        return wdl;
                    }
                }
            }

            // In case we have already searched all the legal moves we don't have to probe
            // the TB because the stored score could be wrong. For instance TB tables
            // do not contain information on position with Enpassant rights, so in this case
            // the state of probe_wdl_table is wrong. Also in case of only capture
            // moves, for instance here 4K3/4q3/6p1/2k5/6p1/8/8/8 w - - 0 7, we have to
            // return with ZEROING_BEST_MOVE set.
            bool completed = (   0 != move_count
                              && move_count == move_list.size());

            WDLScore wdl;
            if (completed)
            {
                wdl = best_wdl;
            }
            else
            {
                wdl = probe_table<WDL>(pos, state);
                if (ProbeState::FAILURE == state)
                {
                    return WDLScore::DRAW;
                }
            }

            // DTZ stores a "don't care" wdl if best_wdl is a win
            if (best_wdl >= wdl)
            {
                state = best_wdl > WDLScore::DRAW
                     || completed ?
                            ProbeState::ZEROING_BEST_MOVE :
                            ProbeState::SUCCESS;
                return best_wdl;
            }

            state = ProbeState::SUCCESS;
            return wdl;
        }

    } // namespace


    /// Probe the WDL table for a particular position.
    /// If state != FAILURE, the probe was successful.
    /// The return value is from the point of view of the side to move:
    /// -2 : loss
    /// -1 : loss, but draw under 50-move rule
    ///  0 : draw
    ///  1 : win, but draw under 50-move rule
    ///  2 : win
    WDLScore probe_wdl(Position &pos, ProbeState &state)
    {
        return search(pos, state, false);
    }

    /// Probe the DTZ table for a particular position.
    /// If *result != FAILURE, the probe was successful.
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
    i32      probe_dtz(Position &pos, ProbeState &state)
    {
        state = ProbeState::SUCCESS;
        auto wdl = search(pos, state, true);

        if (   ProbeState::FAILURE == state
            || WDLScore::DRAW == wdl) // DTZ tables don't store draws
        {
            return 0;
        }

        // DTZ stores a 'don't care' value in this case, or even a plain wrong
        // one as in case the best move is a losing Enpassant, so it cannot be probed.
        if (ProbeState::ZEROING_BEST_MOVE == state)
        {
            return dtz_before_zeroing(wdl);
        }

        i32 dtz = probe_table<DTZ>(pos, state, wdl);

        if (ProbeState::FAILURE == state)
        {
            return 0;
        }

        if (ProbeState::CHANGE_STM != state)
        {
            return (dtz + 100 * (   WDLScore::BLESSED_LOSS == wdl
                                 || WDLScore::CURSED_WIN   == wdl)) * sign(wdl);
        }

        // DTZ stores results for the other side, so we need to do a 1-ply search and
        // find the winning move that minimizes DTZ.
        StateInfo si;
        i32 min_dtz = 0xFFFF;

        for (auto &vm : MoveList<GenType::LEGAL>(pos))
        {
            bool zeroing = pos.capture(vm)
                        || PAWN == ptype(pos[org_sq(vm)]);

            pos.do_move(vm, si);

            // For zeroing moves we want the dtz of the move _before_ doing it,
            // otherwise we will get the dtz of the next move sequence. Search the
            // position after the move to get the score sign(because even in a
            // winning position we could make a losing capture or going for a draw).
            dtz = zeroing ?
                    -dtz_before_zeroing(search(pos, state, false)) :
                    -probe_dtz(pos, state);

            // If the move mates, force minDTZ to 1
            if (   1 == dtz
                && 0 != pos.si->checkers
                && 0 == MoveList<LEGAL>(pos).size())
            {
                min_dtz = 1;
            }

            // Convert state from 1-ply search. Zeroing moves are already accounted
            // by dtz_before_zeroing() that returns the DTZ of the previous move.
            if (!zeroing)
            {
                dtz += sign(dtz);
            }

            // Skip the draws and if we are winning only pick positive dtz
            if (sign(dtz) == sign(wdl))
            {
                min_dtz = std::min(dtz, min_dtz);
            }

            pos.undo_move(vm);

            if (ProbeState::FAILURE == state)
            {
                return 0;
            }
        }

        // When there are no legal moves, the position is mate: return -1
        return min_dtz == 0xFFFF ? -1 : min_dtz;
    }

    /// Use the WDL tables to filter out moves that don't preserve the win or draw.
    /// This is a fall back for the case that some or all DTZ tables are missing.
    ///
    /// A return value false indicates that not all probes were successful and that
    /// no moves were filtered out.
    bool root_probe_wdl(Position &root_pos, RootMoves &root_moves)
    {
        bool rule50 = bool(Options["SyzygyUseRule50"]);

        StateInfo si;
        ProbeState state;
        // Probe and rank each move
        for (auto &rm : root_moves)
        {
            auto move = rm.front();
            root_pos.do_move(move, si);

            WDLScore wdl = -probe_wdl(root_pos, state);

            root_pos.undo_move(move);

            if (ProbeState::FAILURE == state)
            {
                return false;
            }

            rm.tb_rank = WDL_To_Rank[wdl + 2];

            if (!rule50)
            {
                wdl =  wdl > WDLScore::DRAW ? WDLScore::WIN :
                       wdl < WDLScore::DRAW ? WDLScore::LOSS : WDLScore::DRAW;
            }
            rm.tb_value = WDL_To_Value[wdl + 2];
        }
        return true;
    }

    // Use the DTZ tables to rank root moves.
    //
    // A return value false indicates that not all probes were successful.
    bool root_probe_dtz(Position &root_pos, RootMoves &root_moves)
    {
        assert(0 != root_moves.size());

        // Obtain 50-move counter for the root position
        auto clock_ply = root_pos.si->clock_ply;
        // Check whether a position was repeated since the last zeroing move.
        bool rep = root_pos.repeated();

        i16 bound = bool(Options["SyzygyUseRule50"]) ? 900 : 1;
        i32 dtz;

        StateInfo si;
        ProbeState state;
        // Probe and rank each move
        for (auto &rm : root_moves)
        {
            auto move = rm.front();
            root_pos.do_move(move, si);

            // Calculate dtz for the current move counting from the root position
            if (0 == root_pos.si->clock_ply)
            {
                // In case of a zeroing move, dtz is one of -101/-1/0/+1/+101
                WDLScore wdl = -probe_wdl(root_pos, state);
                dtz = dtz_before_zeroing(wdl);
            }
            else
            {
                // Otherwise, take dtz for the new position and correct by 1 ply
                dtz = -probe_dtz(root_pos, state);
                dtz = dtz > 0 ? dtz + 1 :
                      dtz < 0 ? dtz - 1 :
                      dtz;
            }
            // Make sure that a mating move is assigned a dtz value of 1
            if (   0 != root_pos.si->checkers
                && 2 == dtz
                && 0 == MoveList<LEGAL>(root_pos).size())
            {
                dtz = 1;
            }

            root_pos.undo_move(move);

            if (ProbeState::FAILURE == state)
            {
                return false;
            }

            // Better moves are ranked higher. Certain wins are ranked equally.
            // Losing moves are ranked equally unless a 50-move draw is in sight.
            i16 r = dtz > 0 ? (+dtz + clock_ply <= 99 && !rep ? +1000 : +1000 - (+dtz + clock_ply)) :
                    dtz < 0 ? (-dtz * 2 + clock_ply < 100     ? -1000 : -1000 + (-dtz + clock_ply)) :
                    0;
            rm.tb_rank = r;

            // Determine the score to be displayed for this move. Assign at least
            // 1 cp to cursed wins and let it grow to 49 cp as the positions gets
            // closer to a real win.
            rm.tb_value = r >= bound ? +VALUE_MATE - (DEP_MAX + 1) :
                          r >  0     ? (VALUE_EG_PAWN * std::max(+3, r - 800)) / 200 :
                          r == 0     ? VALUE_DRAW :
                          r > -bound ? (VALUE_EG_PAWN * std::min(-3, r + 800)) / 200 :
                                       -VALUE_MATE + (DEP_MAX + 1);
        }
        return true;
    }

    /// Initializes TB
    void initialize(const string &paths)
    {
        return; // TODO:: remove
        static bool initialized = false;

        if (!initialized)
        {
            // MapB1H1H7[] encodes a square below a1-h8 diagonal to 0..27
            i32 code = 0;
            for (auto s : SQ)
            {
                if (off_A1H8(s) < 0)
                {
                    MapB1H1H7[s] = code++;
                }
            }
            // MapA1D1D4[] encodes a square in the a1-d1-d4 triangle to 0..9
            code = 0;
            vector<Square> diagonal;
            for (auto s : { SQ_A1, SQ_B1, SQ_C1, SQ_D1,
                            SQ_A2, SQ_B2, SQ_C2, SQ_D2,
                            SQ_A3, SQ_B3, SQ_C3, SQ_D3,
                            SQ_A4, SQ_B4, SQ_C4, SQ_D4 })
            {
                if (off_A1H8(s) < 0)
                {
                    MapA1D1D4[s] = code++;
                }
                else
                if (off_A1H8(s) == 0)
                {
                    diagonal.push_back(s);
                }
            }
            // Diagonal squares are encoded as last ones
            for (auto s : diagonal)
            {
                MapA1D1D4[s] = code++;
            }
            // MapKK[] encodes all the 461 possible legal positions of two kings where the first is in the a1-d1-d4 triangle.
            // If the first king is on the a1-d4 diagonal, the other one shall not to be above the a1-h8 diagonal.
            vector<pair<i32, Square>> both_on_diagonal;
            code = 0;
            for (i32 idx = 0; idx < 10; ++idx)
            {
                for (auto s1 : { SQ_A1, SQ_B1, SQ_C1, SQ_D1,
                                 SQ_A2, SQ_B2, SQ_C2, SQ_D2,
                                 SQ_A3, SQ_B3, SQ_C3, SQ_D3,
                                 SQ_A4, SQ_B4, SQ_C4, SQ_D4 })
                {
                    if (   MapA1D1D4[s1] == idx
                        && (0 != idx || SQ_B1 == s1)) // SQ_B1 is mapped to 0
                    {
                        for (auto s2 : SQ)
                        {
                            if (contains(PieceAttacks[KING][s1] | s1, s2))
                            {
                                continue; // Illegal position
                            }
                            else
                            if (off_A1H8(s1) == 0 && off_A1H8(s2) > 0)
                            {
                                continue; // First on diagonal, second above
                            }
                            else
                            if (off_A1H8(s1) == 0 && off_A1H8(s2) == 0)
                            {
                                both_on_diagonal.emplace_back(std::make_pair (idx, s2));
                            }
                            else
                            {
                                MapKK[idx][s2] = code++;
                            }
                        }
                    }
                }
            }

            // Legal positions with both kings on diagonal are encoded as last ones
            for (auto p : both_on_diagonal)
            {
                MapKK[p.first][p.second] = code++;
            }

            // Binomial[] stores the Binomial Coefficients using Pascal rule. There
            // are Binomial[k][n] ways to choose k elements from a set of n elements.
            Binomial[0][0] = 1;

            for (i32 n = 1; n < 64; ++n) // Squares
            {
                for (i32 k = 0; k < 6 && k <= n; ++k) // Pieces
                {
                    Binomial[k][n] = (k > 0 ? Binomial[k - 1][n - 1] : 0)
                                   + (k < n ? Binomial[k][n - 1] : 0);
                }
            }

            // MapPawns[s] encodes squares a2-h7 to 0..47. This is the number of possible
            // available squares when the leading one is in square. Moreover the pawn with
            // highest MapPawns[] is the leading pawn, the one nearest the edge and,
            // among pawns with same file, the one with lowest rank.
            i32 available_squares = 47; // Available squares when lead pawn is in a2

            // Init the tables for the encoding of leading pawns group:
            // with 6-men TB can have up to 4 leading pawns (KPPPPK).
            for (i32 lead_pawn_count = 1; lead_pawn_count <= 4; ++lead_pawn_count)
            {
                for (auto f : { F_A, F_B, F_C, F_D })
                {
                    // Restart the index at every file because TB table is splitted
                    // by file, so we can reuse the same index for different files.
                    i32 idx = 0;

                    // Sum all possible combinations for a given file, starting with
                    // the leading pawn on rank 2 and increasing the rank.
                    for (auto r : { R_2, R_3, R_4, R_5, R_6, R_7 })
                    {
                        auto sq = f|r;

                        // Compute MapPawns[] at first pass.
                        // If sq is the leading pawn square, any other pawn cannot be
                        // below or more toward the edge of sq. There are 47 available
                        // squares when sq = a2 and reduced by 2 for any rank increase
                        // due to mirroring: sq == a3 -> no a2, h2, so MapPawns[a3] = 45
                        if (lead_pawn_count == 1)
                        {
                            MapPawns[ sq] = available_squares--;
                            MapPawns[!sq] = available_squares--; // Horizontal flip
                        }
                        LeadPawnIdx[lead_pawn_count][sq] = idx;
                        idx += Binomial[lead_pawn_count - 1][MapPawns[sq]];
                    }
                    // After a file is traversed, store the cumulated per-file index
                    LeadPawnsSize[lead_pawn_count][f] = idx;
                }
            }
            initialized = true;
        }

        TB_Tables.clear();
        MaxLimitPiece = 0;

        PathString = paths;
        if (white_spaces(PathString))
        {
            return;
        }

        // PathString Example
        // (Windows)= D:\tb\wdl345;D:\tb\wdl6;D:\tb\dtz345;D:\tb\dtz6
        // (Unix-based OS)= .\tb\wdl345:.\tb\wdl6:.\tb\dtz345:.\tb\dtz6

        constexpr char SepChar =
#       if defined(_WIN32)
            ';';
#       else
            ':';
#       endif

        //TBFile::Paths = split (PathString, SepChar, false, true);
        TBFile::Paths.clear();
        stringstream ss{PathString};
        string path;
        while (std::getline(ss, path, SepChar))
        {
            if (!white_spaces(path))
            {
                trim(path);
                std::replace(path.begin(), path.end(), '\\', '/');
                TBFile::Paths.push_back(path);
            }
        }

        for (auto p1 = PAWN; p1 < KING; ++p1)
        {
            TB_Tables.add({ KING, p1, KING });

            for (auto p2 = PAWN; p2 <= p1; ++p2)
            {
                TB_Tables.add({ KING, p1, p2, KING });
                TB_Tables.add({ KING, p1, KING, p2 });

                for (auto p3 = PAWN; p3 <= p2; ++p3)
                {
                    TB_Tables.add({ KING, p1, p2, p3, KING });

                    for (auto p4 = PAWN; p4 <= p3; ++p4)
                    {
                        TB_Tables.add({ KING, p1, p2, p3, p4, KING });

                        //for (auto p5 = PAWN; p5 <= p4; ++p5)
                        //{
                        //    TB_Tables.add({ KING, p1, p2, p3, p4, p5, KING });
                        //}
                        //for (auto p5 = PAWN; p5 < KING; ++p5)
                        //{
                        //    TB_Tables.add({ KING, p1, p2, p3, p4, KING, p5 });
                        //}
                    }
                    for (auto p4 = PAWN; p4 < KING; ++p4)
                    {
                        TB_Tables.add({ KING, p1, p2, p3, KING, p4 });

                        //for (auto p5 = PAWN; p5 <= p4; ++p5)
                        //{
                        //    TB_Tables.add({ KING, p1, p2, p3, KING, p4, p5 });
                        //}
                    }
                }
                for (auto p3 = PAWN; p3 < KING; ++p3)
                {
                    TB_Tables.add({ KING, p1, p2, KING, p3 });
                }
                for (auto p3 = PAWN; p3 <= p1; ++p3)
                {
                    for (auto p4 = PAWN; p4 <= (p1 == p3 ? p2 : p3); ++p4)
                    {
                        TB_Tables.add({ KING, p1, p2, KING, p3, p4 });
                    }
                }
            }
        }

        sync_cout << "info string Tablebases found " << TB_Tables.size() << sync_endl;
    }
}

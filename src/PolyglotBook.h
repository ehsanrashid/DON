#ifndef _POLYGLOT_BOOK_H_INC_
#define _POLYGLOT_BOOK_H_INC_

#include <fstream>

#include "Type.h"

class Position;

namespace OpeningBook {

    // A Polyglot book is a series of entries of 16 bytes.
    // All integers are stored in big-endian format,
    // with the highest byte first (regardless of size).
    // The entries are ordered according to the key in ascending order.
    // Polyglot book file has *.bin extension
    class PolyglotBook
        : public std::fstream
    {
    public:
        // Polyglot Book entry needs 16 bytes to be stored.
        //  - Key       8 bytes
        //  - Move      2 bytes
        //  - Weight    2 bytes
        //  - Learn     4 bytes
        struct PBEntry
        {
            static const u08 Size;
            static const PBEntry NullEntry;

            u64 key     = U64(0);
            u16 move    = MOVE_NONE;
            u16 weight  = 0;
            u32 learn   = 0;

            PBEntry () = default;
            PBEntry (u64 k, u16 m, u16 w, u32 l)
                : key (k)
                , move (m)
                , weight (w)
                , learn (l)
            {}

            explicit operator Move () const { return Move(move); }

            bool operator== (const PolyglotBook::PBEntry &pe)
            {
                return key == pe.key
                    && move == pe.move
                    && weight == pe.weight;
            }
            bool operator!= (const PolyglotBook::PBEntry &pe)
            {
                //return !(*this == pe);
                return key != pe.key
                    || move != pe.move
                    || weight != pe.weight;
            }
            bool operator>  (const PolyglotBook::PBEntry &pe)
            {
                return key != pe.key ?
                        key > pe.key :
                        //move > pe.move;      // order by move
                        weight > pe.weight;  // order by weight
            }
            bool operator<  (const PolyglotBook::PBEntry &pe)
            {
                return key != pe.key ?
                        key < pe.key :
                        //move < pe.move;      // order by move
                        weight < pe.weight;  // order by weight
            }
            bool operator>= (const PolyglotBook::PBEntry &pe)
            {
                return key != pe.key ?
                        key >= pe.key :
                        //move >= pe.move;      // order by move
                        weight >= pe.weight;  // order by weight
            }
            bool operator<= (const PolyglotBook::PBEntry &pe)
            {
                return key != pe.key ?
                        key <= pe.key :
                        //move <= pe.move;      // order by move
                        weight <= pe.weight;  // order by weight
            }

            bool operator== (Move m) { return move == m; }
            bool operator!= (Move m) { return move != m; }

            explicit operator std::string () const;

        };

        static const u08 HeaderSize = 96;

    private:

        std::string _filename = "";
        openmode    _mode     = openmode(0);
        size_t      _size     = 0UL;

        template<class T>
        PolyglotBook& operator>> (      T &t);
        template<class T>
        PolyglotBook& operator<< (const T &t);

    public:
        PolyglotBook () = default;
        PolyglotBook (const std::string &filename, openmode mode);

        PolyglotBook (const PolyglotBook&) = delete;
        PolyglotBook& operator= (const PolyglotBook&) = delete;

        ~PolyglotBook ();

        std::string filename () const { return _filename; }

        size_t size ()
        {
            if (_size != 0) return _size;

            size_t cur_pos = tellg ();
            seekg (0L, ios_base::end);
            _size = tellg ();
            seekg (cur_pos, ios_base::beg);
            clear ();
            return _size;
        }

        bool open (const std::string &filename, openmode mode);
        void close ();

        size_t find_index (      Key key);
        size_t find_index (const Position &pos);
        size_t find_index (const std::string &fen, bool c960 = false);

        Move probe_move (const Position &pos, bool pick_best = true);

        std::string read_entries (const Position &pos);

    };

    template<class CharT, class Traits>
    inline std::basic_ostream<CharT, Traits>&
        operator<< (std::basic_ostream<CharT, Traits> &os, const PolyglotBook::PBEntry &pbe)
    {
        os << std::string(pbe);
        return os;
    }

}

#endif // _POLYGLOT_BOOK_H_INC_

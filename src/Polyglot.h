#ifndef _POLYGLOT_H_INC_
#define _POLYGLOT_H_INC_

#include <fstream>

#include "Type.h"

class Position;

namespace Polyglot {

    // Polyglot::Entry needs 16 bytes to be stored.
    //  - Key       8 bytes
    //  - Move      2 bytes
    //  - Weight    2 bytes
    //  - Learn     4 bytes
    struct Entry
    {
        static const u08 Size;
        static const Entry NullEntry;

        u64 key     = U64 (0);
        u16 move    = MOVE_NONE;
        u16 weight  = 0;
        u32 learn   = 0;

        Entry () = default;
        Entry (u64 k, u16 m, u16 w, u32 l)
            : key (k)
            , move (m)
            , weight (w)
            , learn (l)
        {}

        explicit operator Move () const { return Move (move); }

        bool operator== (const Entry &pe)
        {
            return key == pe.key
                && move == pe.move
                && weight == pe.weight;
        }
        bool operator!= (const Entry &pe)
        {
            //return !(*this == pe);
            return key != pe.key
                || move != pe.move
                || weight != pe.weight;
        }
        bool operator>  (const Entry &pe)
        {
            return key != pe.key ?
                key > pe.key :
            //move > pe.move;      // order by move
            weight > pe.weight;  // order by weight
        }
        bool operator<  (const Entry &pe)
        {
            return key != pe.key ?
                key < pe.key :
                //move < pe.move;      // order by move
                weight < pe.weight;  // order by weight
        }
        bool operator>= (const Entry &pe)
        {
            return key != pe.key ?
                key >= pe.key :
                //move >= pe.move;      // order by move
                weight >= pe.weight;  // order by weight
        }
        bool operator<= (const Entry &pe)
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

    // Polyglot::Book is a file containing series of Polyglot::Entry.
    // All integers are stored in big-endian format,
    // with the highest byte first (regardless of size).
    // The entries are ordered according to the key in ascending order.
    // Polyglot::Book file has *.bin extension.
    class Book
        : public std::fstream
    {
    public:

        static const u08 HeaderSize = 96;

    private:

        std::string _filename = "";
        openmode    _mode     = openmode(0);
        size_t      _size     = 0UL;

        template<class T>
        Book& operator>> (      T &t);
        template<class T>
        Book& operator<< (const T &t);

    public:
        Book () = default;
        Book (const std::string &filename, openmode mode);

        Book (const Book&) = delete;
        Book& operator= (const Book&) = delete;

        ~Book ();

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
        operator<< (std::basic_ostream<CharT, Traits> &os, const Entry &pe)
    {
        os << std::string(pe);
        return os;
    }

}

#endif // _POLYGLOT_H_INC_

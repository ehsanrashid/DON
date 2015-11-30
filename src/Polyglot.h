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

        u64 key     = U64(0);
        u16 move    = 0;
        u16 weight  = 0;
        u32 learn   = 0;

        Entry () = default;
        Entry (u64 k, u16 m, u16 w, u32 l)
            : key (k)
            , move (m)
            , weight (w)
            , learn (l)
        {}

        Entry& operator= (const Entry&) = default;

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
            return
                key != pe.key ?
                    key > pe.key :
                    weight != pe.weight ?
                        weight > pe.weight :
                        move > pe.move;
        }
        bool operator<  (const Entry &pe)
        {
            return
                key != pe.key ?
                    key < pe.key :
                    weight != pe.weight ?
                        weight < pe.weight :
                        move < pe.move;
        }
        bool operator>= (const Entry &pe)
        {
            return
                key != pe.key ?
                    key >= pe.key :
                    weight != pe.weight ?
                        weight >= pe.weight :
                        move >= pe.move;
        }
        bool operator<= (const Entry &pe)
        {
            return
                key != pe.key ?
                    key <= pe.key :
                    weight != pe.weight ?
                        weight <= pe.weight :
                        move <= pe.move;
        }

        bool operator== (Move m) { return move == m; }
        bool operator!= (Move m) { return move != m; }

        explicit operator std::string () const;

    };

    template<class CharT, class Traits>
    inline std::basic_ostream<CharT, Traits>&
        operator<< (std::basic_ostream<CharT, Traits> &os, const Entry &pe)
    {
        os << std::string(pe);
        return os;
    }

    // Polyglot::Book is a file containing series of Polyglot::Entry.
    // All integers are stored in big-endian format,
    // with the highest byte first (regardless of size).
    // The entries are ordered according to the key in ascending order.
    // Polyglot::Book file has *.bin extension.
    class Book
        : public std::fstream
    {
    private:

        std::string _book_fn = "";
        openmode    _mode    = openmode(0);
        size_t      _size    = 0U;

    public:
        static const u08 HeaderSize;

        Book () = default;
        Book (const std::string &book_fn, openmode mode);

        Book (const Book&) = delete;
        Book& operator= (const Book&) = delete;

        ~Book ();

        std::string book_fn () const { return _book_fn; }

        size_t size ()
        {
            if (_size != 0U) return _size;

            size_t cur_pos = tellg ();
            seekg (0L, ios_base::end);
            _size = tellg ();
            seekg (cur_pos, ios_base::beg);
            clear ();
            return _size;
        }

        bool open (const std::string &book_fn, openmode mode);
        void close ();

        template<class T>
        Book& operator>> (      T &t);
        template<class T>
        Book& operator<< (const T &t);

        size_t find_index (const Key key);
        size_t find_index (const Position &pos);
        size_t find_index (const std::string &fen, bool c960 = false);

        Move probe_move (const Position &pos, bool pick_best = true);

        std::string read_entries (const Position &pos);

    };

}

#endif // _POLYGLOT_H_INC_

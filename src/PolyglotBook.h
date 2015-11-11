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
            u64 key     = U64(0);
            u16 move    = MOVE_NONE;
            u16 weight  = 0;
            u32 learn   = 0;

            operator std::string () const;

            template<class CharT, class Traits>
            friend std::basic_ostream<CharT, Traits>&
                operator<< (std::basic_ostream<CharT, Traits> &os, const PBEntry &pbe)
            {
                os << std::string(pbe);
                return os;
            }

        };

        static const size_t EntrySize;
        static const size_t HeaderSize;

    private:

        std::string _book_fn;
        openmode    _mode;
        size_t      _size;

        template<class T>
        PolyglotBook& operator>> (T &t);
        template<class T>
        PolyglotBook& operator<< (T &t);

    public:
        // find_index() takes a hash-key as input, and search through the book file for the given key.
        // Returns the index of the 1st book entry with the same key as the input.
        size_t find_index (const Key key);
        size_t find_index (const Position &pos);
        size_t find_index (const std::string &fen, bool c960 = false);

        PolyglotBook ();
        PolyglotBook (const std::string &book_fn, openmode mode);

        PolyglotBook (const PolyglotBook&) = delete;
        PolyglotBook& operator= (const PolyglotBook&) = delete;

        ~PolyglotBook ();

        bool open (const std::string &book_fn, openmode mode);

        void close () { if (is_open ()) std::fstream::close (); }

        std::string filename () const { return _book_fn; }

        size_t size ()
        {
            if (0 >= _size)
            {
                size_t cur_pos = tellg ();
                seekg (0L, end);
                _size = tellg ();
                seekg (cur_pos, beg);
                clear ();
            }
            return _size;
        }

        // probe_move() tries to find a book move for the given position.
        // If no move is found returns MOVE_NONE.
        // If pick_best is true returns always the highest rated move,
        // otherwise randomly chooses one, based on the move score.
        Move probe_move (const Position &pos, bool pick_best = true);

        std::string read_entries (const Position &pos);

    };

}

#endif // _POLYGLOT_BOOK_H_INC_

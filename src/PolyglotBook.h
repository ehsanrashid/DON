#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _POLYGLOT_BOOK_H_INC_
#define _POLYGLOT_BOOK_H_INC_

#include <fstream>

#include "Type.h"
#include "RKISS.h"
#include "noncopyable.h"

class Position;

#ifdef _MSC_VER
#   pragma warning (push)
#   pragma warning (disable: 4250)
#endif

// A Polyglot book is a series of entries of 16 bytes.
// All integers are stored in big-endian format,
// with the highest byte first (regardless of size).
// The entries are ordered according to the key in ascending order.
// Polyglot book file has *.bin extension
class PolyglotBook
    : private std::fstream
    , public std::noncopyable
{

public:

    // Polyglot Book entry needs 16 bytes to be stored.
    //  - Key       8 bytes
    //  - Move      2 bytes
    //  - Weight    2 bytes
    //  - Learn     4 bytes
    struct PBEntry
    {
        u64 key;
        u16 move;
        u16 weight;
        u32 learn;

        PBEntry ()
            : key (U64 (0))
            , move (MOVE_NONE)
            , weight (0)
            , learn (0)
        {}

        operator std::string () const;

        template<class charT, class Traits>
        friend std::basic_ostream<charT, Traits>&
            operator<< (std::basic_ostream<charT, Traits> &os, const PBEntry &pbe)
        {
            os << std::string (pbe);
            return os;
        }

    };

    static const u08 PGENTRY_SIZE   = sizeof (PBEntry);
    static const u08 PGHEADER_SIZE  = 0*PGENTRY_SIZE;

    static const u64 ERROR_INDEX    = u64 (-1);

private:

    std::string _fn_book;
    std::ios_base::openmode _mode;

    u64    _size_book;

    RKISS       _rkiss;

    template<class T>
    PolyglotBook& operator>> (T &t);
    template<class T>
    PolyglotBook& operator<< (T &t);

public:
    // find_index() takes a hash-key as input, and search through the book file for the given key.
    // Returns the index of the 1st book entry with the same key as the input.
    u64 find_index (const Key key);
    u64 find_index (const Position &pos);
    u64 find_index (const std::string &fen, bool c960 = false);

public:

    PolyglotBook ();

    // mode = std::ios_base::in|std::ios_base::out
    PolyglotBook (const std::string &fn_book, std::ios_base::openmode mode);
    ~PolyglotBook ();

    bool open (const std::string &fn_book, std::ios_base::openmode mode);
    void close ();

    std::string filename () const { return _fn_book; }

    u64 size ()
    {
        if (0 >= _size_book)
        {
            u64 pos_cur = tellg ();
            seekg (0L, std::ios_base::end);
            _size_book = tellg ();
            seekg (pos_cur, std::ios_base::beg);
            clear ();
        }
        return _size_book;
    }

    bool is_open () const { return std::fstream::is_open (); }

    // probe_move() tries to find a book move for the given position.
    // If no move is found returns MOVE_NONE.
    // If pick_best is true returns always the highest rated move,
    // otherwise randomly chooses one, based on the move score.
    Move probe_move (const Position &pos, bool pick_best = true);

    std::string read_entries (const Position &pos);

    void insert_entry (const PolyglotBook::PBEntry &pbe);

    void write ();

    //void import_pgn (const std::string &fn_pgn);
    //void merge_book (const std::string &fn_book);
    //void dump ();
    //void info ();

};

#ifdef _MSC_VER
#   pragma warning (pop)
#endif

#endif // _POLYGLOT_BOOK_H_INC_

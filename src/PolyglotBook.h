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
typedef class PolyglotBook
    : private std::fstream
    , public std::noncopyable
{

public:

    // Polyglot entry needs 16 bytes to be stored.
    //  - Key       8 bytes
    //  - Move      2 bytes
    //  - Weight    2 bytes
    //  - Learn     4 bytes
    typedef struct PEntry
    {
        uint64_t key;
        uint16_t move;
        uint16_t weight;
        uint32_t learn;

        PEntry ()
            : key (U64 (0))
            , move (MOVE_NONE)
            , weight (0)
            , learn (0)
        {}

        operator std::string () const;

        template<class charT, class Traits>
        friend std::basic_ostream<charT, Traits>&
            operator<< (std::basic_ostream<charT, Traits> &os, const PEntry &pe)
        {
            os << std::string (pe);
            return os;
        }

    } PEntry;

    static const uint8_t SIZE_PGENTRY   = sizeof (PEntry);
    static const uint8_t SIZE_PGHEADER  = 0*SIZE_PGENTRY;

    static const uint64_t  ERROR_INDEX  = uint64_t (-1);

private:

    std::string _fn_book;
    std::ios_base::openmode _mode;

    uint64_t    _size_book;

    RKISS       _rkiss;

    template<class T>
    PolyglotBook& operator>> (T &t);
    template<class T>
    PolyglotBook& operator<< (T &t);

public:
    // find_index() takes a hash-key as input, and search through the book file for the given key.
    // Returns the index of the 1st book entry with the same key as the input.
    uint64_t find_index (const Key key);
    uint64_t find_index (const Position &pos);
#ifndef NDEBUG
    uint64_t find_index (const        char *fen, bool c960 = false);
#endif
    uint64_t find_index (const std::string &fen, bool c960 = false);

public:

    PolyglotBook();
    // mode = std::ios_base::in | std::ios_base::out
#ifndef NDEBUG
    PolyglotBook (const        char *fn_book, std::ios_base::openmode mode);
#endif
    PolyglotBook (const std::string &fn_book, std::ios_base::openmode mode);
    ~PolyglotBook ();

#ifndef NDEBUG
    bool open (const        char *fn_book, std::ios_base::openmode mode);
#endif
    bool open (const std::string &fn_book, std::ios_base::openmode mode);
    void close ();

    std::string filename () const { return _fn_book; }

    uint64_t size ()
    {
        if (0 >= _size_book)
        {
            uint64_t pos_cur = tellg ();
            seekg (0L, std::ios_base::end);
            _size_book = tellg ();
            seekg (pos_cur, std::ios_base::beg);
            clear ();
        }
        return _size_book;
    }

    bool is_open () const { return std::fstream::is_open(); }

    // probe_move() tries to find a book move for the given position.
    // If no move is found returns MOVE_NONE.
    // If pick_best is true returns always the highest rated move,
    // otherwise randomly chooses one, based on the move score.
    Move probe_move (const Position &pos, bool pick_best = true);

    std::string read_entries (const Position &pos);

    void insert_entry (const PolyglotBook::PEntry &pe);


    void write ();

    //void import_pgn (const std::string &fn_pgn);

    //void merge_book (const std::string &fn_book);

    //void dump ();
    //void info ();


} PolyglotBook;


#ifdef _MSC_VER
#   pragma warning (pop)
#endif

#endif // _POLYGLOT_BOOK_H_INC_

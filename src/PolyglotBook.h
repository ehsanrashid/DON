//#pragma once
#ifndef POLYGLOT_BOOK_H_
#define POLYGLOT_BOOK_H_

#include <ios>
#include <fstream>
#include "Type.h"
#include "RKISS.h"
#include "noncopyable.h"

class Position;

#pragma warning (push)
#pragma warning (disable: 4250)

// A Polyglot book is a series of entries of 16 bytes.
// All integers are stored in big-endian format, with highest byte first (regardless of size).
// The entries are ordered according to the key in ascending order.
// Polyglot book file has *.bin extension
typedef class PolyglotBook sealed
    : private std::fstream
    , public std::noncopyable
{

public:

    // Polyglot entry needs 16 bytes to be stored.
    //  - Key       8 bytes
    //  - Move      2 bytes
    //  - Weight    2 bytes
    //  - Learn     4 bytes
    typedef struct PolyglotEntry sealed
    {
        uint64_t key;
        uint16_t move;
        uint16_t weight;
        uint32_t learn;

        operator std::string () const;

        template<class charT, class Traits>
        friend std::basic_ostream<charT, Traits>&
            operator<< (std::basic_ostream<charT, Traits> &os, const PolyglotEntry &pe)
        {
            os << std::string (pe);
            return os;
        }

    } PolyglotEntry;

    //typedef struct PolyglotHeader
    //{
    //    //PolyglotEntry p[6]; // 96
    //} PolyglotHeader;


    static const uint8_t SIZE_PGENTRY   = sizeof (PolyglotEntry);
    static const uint8_t SIZE_PGHEADER  = 0*SIZE_PGENTRY;

    static const size_t  ERROR_INDEX      = size_t (-1);

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
    size_t find_index (const Key key);
    size_t find_index (const Position &pos);
    size_t find_index (const        char *fen, bool c960 = false);
    size_t find_index (const std::string &fen, bool c960 = false);

public:

    PolyglotBook();
    // mode = std::ios_base::in | std::ios_base::out
    PolyglotBook (const        char *fn_book, std::ios_base::openmode mode);
    PolyglotBook (const std::string &fn_book, std::ios_base::openmode mode);
    ~PolyglotBook ();

    bool open (const        char *fn_book, std::ios_base::openmode mode);
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

    void insert_entry (const PolyglotBook::PolyglotEntry &pe);


    void write ();

    void import_pgn (const std::string &fn_pgn);

    void merge_book (const std::string &fn_book);

    //void dump ();
    //void info ();


} PolyglotBook;



#pragma warning (pop)

#endif

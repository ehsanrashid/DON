#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _PGN_H_
#define _PGN_H_

#include <fstream>
#include <vector>
#include <stack>
#include "Type.h"
#include "noncopyable.h"


class Game;

#pragma warning (push)
#pragma warning (disable: 4250)

// PGN file with *.pgn extension
typedef class PGN
    : private std::fstream
    , public std::noncopyable
{

private:

    typedef enum PGN_State : int8_t
    {

        PGN_NEW = 0,

        PGN_TAG_NEW,
        PGN_TAG_BEG,
        PGN_TAG_END,

        PGN_MOV_NEW,
        PGN_MOV_LST,
        PGN_MOV_COM,

        PGN_VAR_LST,
        PGN_VAR_COM,
        
        PGN_ERR = 32,

    } PGN_State;

    std::string _fn_pgn;
    std::ios_base::openmode _mode;

    uint64_t    _size_pgn;

    std::vector<uint64_t>   _indexes_game;
    std::stack<char>        _stk_char;

    void _reset ();
    void _build_indexes ();
    void _scan_index (const char buf[], uint64_t &pos, PGN_State &pgn_state);
    void _add_index (uint64_t pos);

public:

    PGN ();
    // mode = std::ios_base::in | std::ios_base::out
    PGN (const          char *fn_pgn, std::ios_base::openmode mode);
    PGN (const std::string &fn_pgn, std::ios_base::openmode mode);
    ~PGN ();

    bool open (const          char *fn_pgn, std::ios_base::openmode mode);
    bool open (const std::string &fn_pgn, std::ios_base::openmode mode);

    void close ();

    uint64_t size ()
    {
        if (0 >= _size_pgn)
        {
            uint64_t pos_cur = tellg ();
            seekg (0L, std::ios_base::end);
            _size_pgn = tellg ();
            seekg (pos_cur, std::ios_base::beg);
            clear ();
        }
        return _size_pgn;
    }

    std::string filename () const { return _fn_pgn; }

    size_t game_count () const { return _indexes_game.size (); }

    std::string read_text (size_t index);
    std::string read_text (size_t index_beg, size_t index_end);
    size_t write_text (const std::string &text);

    Game   read_game (size_t index);
    size_t write_game (const Game &game);

} PGN;

#pragma warning (pop)

#endif // _PGN_H_

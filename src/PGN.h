#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _PGN_H_INC_
#define _PGN_H_INC_

#include <fstream>
#include <vector>
#include <stack>

#include "Type.h"
#include "noncopyable.h"


class Game;

#pragma warning (push)
#pragma warning (disable: 4250)

// PGN file with *.pgn extension
class PGN
    : private std::fstream
    , public std::noncopyable
{

private:

    enum PGN_State : i08
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

    };

    std::string _fn_pgn;
    std::ios_base::openmode _mode;

    u64 _size_pgn;

    std::vector<u64> _indexes_game;
    std::stack<char> _stk_char;

    void _reset ();
    void _build_indexes ();
    void _scan_index (const char *buf, u64 &pos, PGN_State &pgn_state);
    void _add_index (u64 pos);

public:

    PGN ();
    // mode = std::ios_base::in|std::ios_base::out
    PGN (const         char *fn_pgn, std::ios_base::openmode mode);
    PGN (const std::string &fn_pgn, std::ios_base::openmode mode);
    ~PGN ();

    bool open (const         char *fn_pgn, std::ios_base::openmode mode);
    bool open (const std::string &fn_pgn, std::ios_base::openmode mode);

    void close ();

    u64 size ()
    {
        if (0 >= _size_pgn)
        {
            u64 pos_cur = tellg ();
            seekg (0L, std::ios_base::end);
            _size_pgn = tellg ();
            seekg (pos_cur, std::ios_base::beg);
            clear ();
        }
        return _size_pgn;
    }

    std::string filename () const { return _fn_pgn; }

    u64 game_count () const { return _indexes_game.size (); }

    std::string read_text (u64 index);
    std::string read_text (u64 index_beg, u64 index_end);
    u64 write_text (const std::string &text);

    Game   read_game (u64 index);
    u64 write_game (const Game &game);

};

#pragma warning (pop)

#endif // _PGN_H_INC_

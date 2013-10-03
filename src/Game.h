//#pragma once
#ifndef GAME_H_
#define GAME_H_

#include <map>
#include <vector>
#include "functor.h"
#include "Position.h"

typedef enum Result : uint8_t
{
    NO_RES  = 0,
    WIN_W   = 1,
    WIN_B   = 2,
    DRAW    = 3,

} Result;

typedef struct Tag sealed
{
private:

private:
    ::std::string value;

public:

    int8_t index;

    Tag (std::string val, int8_t idx)
        : value (val)
        , index (idx)
    {}

    operator ::std::string () const { return value; }

    //::std::string Tag::to_string () const
    //{
    //    return (*this);
    //}

} Tag;


template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const Tag &tag)
{
    os << ::std::string (tag);
    return os;
}

typedef class Game sealed
{
public:

    typedef::std::map<::std::string, Tag, ::std::string_less_comparer> TagMap;

private:

    TagMap          _map_tag;

    MoveList        _lst_move;
    StateInfoStack  _stk_state;

    Position   _last_pos;
    Result     _result;

    void print_tags (::std::ostream &ostream) const;

public:

    Game ();
    explicit Game (int8_t dummy);

    Game (const          char *text);
    Game (const ::std::string &text);

    //Game (const Game &game);
    //~Game ();
    //Game& operator= (const Game &game);

    Position Position () const { return _last_pos; }

    Result Result ()     const { return _result; }

    void add_tag (const Tag &tag);
    void add_tag (const ::std::string &name, const ::std::string &value);

    bool append_move (Move m);
    bool append_move (const ::std::string &smove);

    bool remove_move ();

    bool setup (const ::std::string &fen, bool c960 = false, bool full = true);

    void clear ();
    void reset ();

    ::std::string to_pgn () const;
    operator ::std::string () const;

    static bool parse (Game &game, const          char *text);
    static bool parse (Game &game, const ::std::string &text);

} Game;



template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const Game &game)
{
    os << ::std::string (game);
    return os;
}

#endif

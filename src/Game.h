#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _GAME_H_
#define _GAME_H_

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

typedef struct Tag
{
private:

private:
    std::string value;

public:

    int8_t index;

    Tag (std::string val, int8_t idx)
        : value (val)
        , index (idx)
    {}

    operator std::string () const { return value; }

    //std::string Tag::to_string () const
    //{
    //    return (*this);
    //}

    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const Tag &tag)
    {
        os << std::string (tag);
        return os;
    }

} Tag;


typedef class Game
{
public:

    typedef std::map<std::string, Tag, std::string_less_nocase_comparer> TagMap;

private:

    TagMap              _map_tag;

    std::vector<Move>   _lst_move;
    StateInfoStack      _stk_state;

    Position   _last_pos;
    Result     _result;

    void print_tags (std::ostream &ostream) const;

public:

    Game ();
    explicit Game (int8_t dummy);

    Game (const          char *text);
    Game (const std::string &text);

    //Game (const Game &game);
    //~Game ();
    //Game& operator= (const Game &game);

    Position Position () const { return _last_pos; }

    Result Result ()     const { return _result; }

    void add_tag (const Tag &tag);
    void add_tag (const std::string &name, const std::string &value);

    bool append_move (Move m);
    bool append_move (const std::string &smove);

    bool remove_move ();

    bool setup (const std::string &fen, bool c960 = false, bool full = true);

    void clear ();
    void reset ();

    std::string pgn () const;
    operator std::string () const;

    static bool parse (Game &game, const          char *text);
    static bool parse (Game &game, const std::string &text);

    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const Game &game)
    {
        os << std::string (game);
        return os;
    }

    template<class charT, class Traits>
    friend std::basic_istream<charT, Traits>&
        operator>> (std::basic_istream<charT, Traits> &is, Game &game)
    {
        //is >> std::string (game);
        return is;
    }


} Game;

#endif // _GAME_H_

#include "Game.h"

#include <sstream>
#include <regex>
#include <iterator>

using namespace std;

Game::Game ()
    : _last_pos (FEN_N)
    , _result (NO_RES)
{}

Game::Game (i08 dummy)
{}

Game::Game (const char   *text)
{
    Game game (i08 (0));
    if (parse (game, text))
    {
        *this = game;
    }
    else
    {
        clear ();
    }
}
Game::Game (const string &text)
{
    Game game (i08 (0));
    if (parse (game, text))
    {
        *this = game;
    }
    else
    {
        clear ();
    }
}

//Game::Game (const Game &game)
//{
//    *this = game;
//}
//Game::~Game ()
//{}
//Game& Game::operator= (const Game &game)
//{
//    return *this;
//}

void Game::add_tag (const Tag &tag)
{
}
void Game::add_tag (const string &name, const string &value)
{
    //_tag_map[name] = value;
    //p = _tag_map.find (); if (p != _tag_map.end ())

    //    //this line produce error for insert
    //    pair<map<string, string>::iterator, bool> test = _tag_map.insert (make_pair (name, value));
    //if (test.second)
    //{
    //    pair<string, string> tag = *(test.first);
    //    if (equals (tag.first, "fen"))
    //    {
    //        string &fen = tag.second;
    //        setup (fen);
    //    }
    //}


}

bool Game::append_move (Move m)
{
    // TODO:: check legal move
    if (_last_pos.legal (m))
    {
        StateInfo si;
        _state_stk.push (si);
        //_move_list.emplace_back (m);

        _last_pos.do_move (m, _state_stk.top ());

        return true;
    }
    return false;
}
bool Game::append_move (const string &smove)
{
    // TODO::
    return true;
}

// Remove last move
bool Game::remove_move ()
{
    _last_pos.undo_move ();
    //Move m = _state_stk.top ().move_last;
    _state_stk.pop ();
    return true;
}

bool Game::setup (const string &fen, bool c960, bool full)
{
    return _last_pos.setup (fen, NULL, c960, full);
}

void Game::clear ()
{
    _tag_map.clear ();
    _move_list.clear ();
    _last_pos.clear ();
    _result = NO_RES;
}
void Game::reset ()
{
    size_t size = _move_list.size ();
    while (!_move_list.empty ())
    {
        if (!remove_move ())
        {
            //cout << "ERROR: Undo move " << _move_list.back ();
            break;
        }
    }

    if (size != _move_list.size ())
    {
        _result = NO_RES;
    }
}

string Game::pgn () const
{
    ostringstream spgn;
    // pgn format
    print_tags (spgn);

    return spgn.str ();
}

Game::operator string ()  const
{
    ostringstream sgame;
    // tag list
    // starting fen
    // move list
    // last position
    print_tags (sgame);

    return sgame.str ();
}

void Game::print_tags (ostream &ostream) const
{
    for (size_t idx = 0; idx < _tag_map.size (); ++idx)
    {
        Game::TagMap::const_iterator itr = _tag_map.cbegin ();
        while (itr != _tag_map.cend ())
        {
            const Tag &tag = itr->second;
            if (idx == tag.index)
            {
                ostream << "[" << itr->first << " \"" << tag << "\"]" << endl;
            }
            ++itr;
        }
    }
}

bool Game::parse (Game &game, const char   *text)
{
    bool is_ok = false;
    char *c = strdup (text);

    //char name[40];
    //char value[80];
    //i32 n;

    //// Tag section
    //do
    //{
    //    // " [ %s \"%[^\"]\" ] %n" or " [ %[^ ] \"%[^\"]\" ] %n"
    //    i08 read = _snscanf_s (c, 256, " [ %s \"%[^\"]\" ] %n", name, 40, value, 80, &n);
    //    if (read != 2) break;
    //    c += n;

    //    cout << name << " " << value << endl;
    //    //game.AddTag(name, value);
    //}
    //while ('\0' != *c);

    //char *m = c;
    //// modify he move section
    //do
    //{
    //    if (';' == *m)
    //    {
    //        while ('\n' != *m)
    //        {
    //            *m++ = ' ';
    //            if ('\0' == *m) goto end_modify;
    //        }
    //    }

    //    if ('\n' == *m)
    //    {
    //        *m++ = ' ';
    //        //while (' ' == *m) ++m;

    //        if ('%' == *m)
    //        {
    //            while ('\n' != *m)
    //            {
    //                *m++ = ' ';
    //                if ('\0' == *m) goto end_modify;
    //            }
    //            *m++ = ' ';
    //        }
    //    }
    //    ++m;
    //}
    //while ('\0' != *m);

    //end_modify:

    // Move section
    do
    {

        c++;
    }
    while ('\0' != *c);

    free (c);

    return is_ok;
}
bool Game::parse (Game &game, const string &text)
{
    bool is_ok = false;

    // TODO::

    ////string seq("[Event \"Blitz 4m+2s\"]\n[Site \"?\"]\n[Date \"2001.12.05\"]\n[Round \"4\"]\n[White \"Deep Fritz 13\"]\n[Black \"aquil, muzaffar\"]\n[Result \"1/2-1/2\"]\n[ECO \"C80\"]\n[WhiteElo \"2839\"]\n[BlackElo \"2808\"]\n[PlyCount \"37\"]\n");
    char *pat =
        //"[Event \"Blitz 4m+2s\"]\n[Site \"?\"]\n";
        "1. e4 e5 2. Nf3 {a}  {b} Nc6 3. Bb5 a6 4...d5";
    //"11... Bxe3 12. Qxe3 Nxc3  13. Qxc3 {dfs} {sfsf} Qd7 14. Rad1 Nd8";
    string seq (pat);

    string reg_esp =
        /// tag
        //"(?:^\\s*\\[\\s*(\\w+)\\s+\"([^\"]+)\"\\s*\\]\\s*)";
        ///move
        // backtracking
        //"(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(\\d+)(\\.|\\.{3})\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*((?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?))\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*((?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?))\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*)?)";
        // no backtracking
        //"(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(\\d+)(\\.|\\.{3})\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{([^\\}]*?)\\}\\s*)?\\s*)?)";
        // no comment
        "(?:\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(\\d+)(\\.|\\.{3})\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(?:\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*(?:([PNBRQK]?[a-h]?[1-8]?x?[a-h][1-8](?:\\=[NBRQ])?|O(?:-?O){1,2})(?:[+][+]?|[#])?(?:\\s*[!?]+)?)\\s*(?:\\{[^\\}]*?\\}\\s*)?\\s*)?)";

    // endMarker
    //\\s+(1\\-?0|0\\-?1|1\\/2\\-?1\\/2|\\*)\\s+

    //cout << "Target sequence: " << endl << seq << endl;
    //cout << "Regular expression: /" << reg_esp << "/" << endl;
    //cout << "The following matches were found:" << endl;

    regex rgx (reg_esp);//, regex_constants::match_flag_type::match_continuous);
    sregex_iterator begin (seq.begin (), seq.end (), rgx), end;
    //for (auto itr = begin; itr != end; ++itr)
    //{
    //    bool first = true;
    //    for (auto x : (*itr))
    //    {
    //        if (first)
    //        {
    //            first = false;
    //            continue;
    //        }
    //        cout << x << "   ";
    //    }
    //    cout << endl;
    //}

    //cout << "--------" << endl;

    //smatch match;
    //while (regex_search(seq, match, rgx, regex_constants::match_flag_type::match_not_null))
    //{
    //    bool first = false;
    //    
    //    for (sub_match<string::const_iterator> x : match)
    //    for (auto x : match)
    //    {
    //        if (first)
    //        {
    //            first = false;
    //            continue;
    //        }
    //        cout << x << "   ";
    //    }
    //    cout << endl;
    //    seq = match.suffix().str();
    //}

    return is_ok;
}

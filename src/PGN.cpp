#include "PGN.h"

#include <iostream>
#include "xcstring.h"
#include "xstring.h"
#include "Game.h"
#include "TriLogger.h"

PGN::PGN ()
    : ::std::fstream()
    , _fn_pgn ("")
    , _mode (0)
    , _size_pgn (0)
{}
PGN::PGN (const          char *fn_pgn, ::std::ios_base::openmode mode)
    : ::std::fstream (fn_pgn, mode | ::std::ios_base::binary)
    , _fn_pgn (fn_pgn)
    , _mode (mode)
    , _size_pgn (0)
{
    clear (); // Reset any error flag to allow retry open()
    _build_indexes ();
}

PGN::PGN (const ::std::string &fn_pgn, ::std::ios_base::openmode mode)
    : ::std::fstream (fn_pgn, mode | ::std::ios_base::binary)
    , _fn_pgn (fn_pgn)
    , _mode (mode)
    , _size_pgn (0)
{
    clear (); // Reset any error flag to allow retry open()
    _build_indexes ();
}

PGN::~PGN () { close (); }

// open the file in mode
// Read -> ::std::ios_base::in
// Write-> ::std::ios_base::out
bool PGN::open (const          char *fn_pgn, ::std::ios_base::openmode mode)
{
    close ();
    ::std::fstream::open (fn_pgn, mode | ::std::ios_base::binary);
    clear (); // Reset any error flag to allow retry open()
    _fn_pgn = fn_pgn;
    _mode   = mode;
    _build_indexes ();
    return is_open ();
}
bool PGN::open (const ::std::string &fn_pgn, ::std::ios_base::openmode mode)
{
    close ();
    ::std::fstream::open (fn_pgn, mode | ::std::ios_base::binary);
    clear (); // Reset any error flag to allow retry open()
    _fn_pgn = fn_pgn;
    _mode   = mode;
    _build_indexes ();
    return is_open ();
}

void PGN::close () { if (is_open ()) { ::std::fstream::close (); _reset (); } }

void PGN::_reset ()
{
    //_fn_pgn.clear ();
    //_mode       = 0;
    _size_pgn   = 0;
    _indexes_game.clear ();
}

void PGN::_build_indexes ()
{
    if (is_open () && (_mode & ::std::ios_base::in) && good ())
    {
        if (0 < game_count ()) _reset ();

        size ();

        try
        {

            // 32768 = 32*1024 = 32 K
#define MAX_SIZE    32768

            char buf[MAX_SIZE + 1];
            buf[MAX_SIZE] = '\0';

            uint64_t pos = 0;
            PGN_State pgn_state = PGN_NEW;

            // Clear the char stack
            while (!_stk_char.empty ()) _stk_char.pop ();

            seekg (0L);
            do
            {
                ::std::memset (buf, '\0', MAX_SIZE);
                read (buf, MAX_SIZE);
                _scan_index (buf, pos, pgn_state);
            }
            while (!eof () && good () && (PGN_ERR != pgn_state));
            clear ();

            if ((PGN_ERR == pgn_state))
            {
                // error at offset
            }
            if ((PGN_MOV_NEW == pgn_state) || (PGN_MOV_LST == pgn_state))
            {
                _add_index (pos);
            }

#undef MAX_SIZE

        }
        catch (...)
        {}
    }
}

void PGN::_scan_index (const char buf[], uint64_t &pos, PGN_State &pgn_state)
{
    ASSERT (buf);
    size_t length = strlen (buf);

    size_t offset = 0;
    //size_t last_offset = offset;

#undef check_incomplete
#undef skip_whitespace

#define skip_whitespace() do { if (length == offset) goto done; c = buf[offset++]; } while (isspace (c))

#define check_incomplete() do { if (!c) {    \
    ::std::cerr << "ERROR: incomplete game"; \
    pgn_state = PGN_ERR; goto done;          \
    } } while (false)

    while (offset < length)
    {
        unsigned char c;

        skip_whitespace ();

        if (!c)
        {
            if (!_stk_char.empty ())
            {
                ::std::cerr << "ERROR: missing closing character of: " <<
                    _stk_char.top () << "at location: " << (pos + offset);
                pgn_state = PGN_ERR;
                goto done;
            }
            else
            {
                ::std::cerr << ("****SUCCESS****");
            }
            break;
        }
        switch (pgn_state)
        {
        case PGN_NEW:
            switch (c)
            {
            case '\n':
                break;
            case  '[':
                pgn_state = PGN_TAG_BEG;
                break;
            case  '0': case  '1': case  '2': case  '3': case  '4':
            case  '5': case  '6': case  '7': case  '8': case  '9':
            case  '*':
                pgn_state = PGN_MOV_LST;
                break;
            case  '{':
                pgn_state = PGN_MOV_COM;
                break;
            case  ';':
                while ('\n' != c)
                {
                    skip_whitespace ();
                    check_incomplete ();
                }
                pgn_state = PGN_MOV_LST;
                break;
            case  '%':
                while ('\n' != c)
                {
                    skip_whitespace ();
                    check_incomplete ();
                }
                pgn_state = PGN_MOV_LST;
                break;
            default:
                ::std::cerr << ("ERROR: invalid character");
                pgn_state = PGN_ERR;
                goto done;
                break;
            }
            break;

        case PGN_TAG_NEW:
            switch (c)
            {
            case '\n':
                pgn_state = PGN_NEW;
                break;
            case  '[':
                pgn_state = PGN_TAG_BEG;
                break;
            }
            break;

        case PGN_TAG_BEG:
            while (']' != c)
            {
                skip_whitespace ();
                check_incomplete ();
            }
            pgn_state = PGN_TAG_END;
            break;

        case PGN_TAG_END:
            switch (c)
            {
            case '\n':
                pgn_state = PGN_TAG_NEW;
                break;
            case  '[':
                pgn_state = PGN_TAG_BEG;
                break;
            }
            break;

        case PGN_MOV_NEW:
            switch (c)
            {
            case '\n':
                pgn_state = PGN_NEW;
                // use last_offset here
                //last_offset = offset;
                _add_index (pos + offset);
                break;
            case  '(':
                pgn_state = PGN_VAR_LST;
                _stk_char.push (c);
                break;
            case  '{':
                pgn_state = PGN_MOV_COM;
                break;
            case  ';':
                while ('\n' != c)
                {
                    skip_whitespace ();
                    check_incomplete ();
                }
                break;
            case  '%':
                while ('\n' != c)
                {
                    skip_whitespace ();
                    check_incomplete ();
                }
                break;
            default:
                pgn_state = PGN_MOV_LST;
                break;
            }
            break;

        case PGN_MOV_LST:
            switch (c)
            {
            case '\n':
                pgn_state = PGN_MOV_NEW;
                break;
            case  '(':
                pgn_state = PGN_VAR_LST;
                _stk_char.push (c);
                break;
            case  '{':
                pgn_state = PGN_MOV_COM;
                break;
            case  ';':
                while ('\n' != c)
                {
                    skip_whitespace ();
                    check_incomplete ();
                }
                pgn_state = PGN_MOV_NEW;
                break;
            default: break;
            }
            break;

        case PGN_MOV_COM:
            while ('}' != c)
            {
                skip_whitespace ();
                check_incomplete ();
            }
            pgn_state = PGN_MOV_LST;
            break;

        case PGN_VAR_LST:
            switch (c)
            {
            case  '(':
                _stk_char.push (c);
                break;
            case  ')':
                if (_stk_char.empty () || '(' != _stk_char.top ())
                {
                    ::std::cerr << ("ERROR: missing opening of variation");
                    pgn_state = PGN_ERR;
                    goto done;
                }
                else
                {
                    _stk_char.pop ();
                }
                break;
            case  '{':
                pgn_state = PGN_VAR_COM;
                _stk_char.push (c);
                break;
            default:
                if (_stk_char.empty ())
                {
                    pgn_state = PGN_MOV_LST;
                }
                break;
            }
            break;

        case PGN_VAR_COM:
            switch (c)
            {
            case  '}':
                if (_stk_char.empty () || '{' != _stk_char.top ())
                {
                    ::std::cerr << ("ERROR:: missing opening of variation comment");
                    pgn_state = PGN_ERR;
                    goto done;
                }
                else
                {
                    pgn_state = PGN_VAR_LST;
                    _stk_char.pop ();
                }
                break;
            }
            break;

        case PGN_ERR:
            goto done;
            break;

        default:
            break;
        }
    }

#undef check_incomplete
#undef skip_whitespace

done:
    pos += offset;

}

void PGN::_add_index (uint64_t pos)
{
    //size_t g_count = game_count ();
    //if (0 < g_count)
    //{
    //    if (pos <= _indexes_game[g_count - 1]) return;
    //}

    _indexes_game.emplace_back (pos);
}

// Read the text index (1...n)
std::string PGN::read_text (size_t index)
{
    if (1 <= index && index <= game_count ())
    {
        if (is_open () && good ())
        {
            uint64_t pos_beg = (1 == index) ? 0 : _indexes_game[index - 2];
            uint64_t pos_end = _indexes_game[index - 1];

            size_t size = size_t (pos_end - pos_beg);
            //char *buf = new char[(size + 1)];
            //if (buf)
            //{
            //    seekg (pos_beg);
            //    read (buf, size);
            //    buf[size] = '\0';
            //
            //    //remove_substring (buf, "\r");
            //    remove_all (buf, '\r');
            //
            //    ::std::string text = buf;
            //    delete[] buf; buf = NULL;
            //
            //    //remove_substring (text, "\r");
            //    return text;
            //}

            ::std::string text (size, ' ');
            seekg (pos_beg);
            read (&text[0], size);
            remove_substring (text, "\r");
            return text;
        }
    }
    return "";
}
// Read the text index_beg (1...n), index_end (1...n)
std::string PGN::read_text (size_t index_beg, size_t index_end)
{
    size_t g_count = game_count ();

    if (index_beg <= index_end &&
        g_count >= index_beg && index_end <= g_count)
    {
        if (is_open () && good ())
        {
            uint64_t pos_beg = (1 == index_beg) ? 0 : _indexes_game[index_beg - 2];
            uint64_t pos_end = _indexes_game[index_end - 1];

            size_t size = size_t (pos_end - pos_beg);
            //char *buf = new char[(size + 1)];
            //if (buf)
            //{
            //    seekg (pos_beg);
            //    read (buf, size);
            //    buf[size] = '\0';
            //
            //    //remove_substring (buf, "\r");
            //    remove_all (buf, '\r');
            //
            //    ::std::string text = buf;
            //    delete[] buf; buf = NULL;
            //
            //    //remove_substring (text, "\r");
            //    return text;
            //}


            ::std::string text (size, ' ');
            seekg (pos_beg);
            read (&text[0], size);
            remove_substring (text, "\r");
            return text;
        }
    }
    return "";
}
// Write the text and return index of the text
size_t PGN::write_text (const ::std::string &text)
{
    if (is_open () && good ())
    {

    }
    return 0;
}
// Read the game from index (1...n)
Game   PGN::read_game (size_t index)
{
    return Game (read_text (index));
}
// Write the game and return index of the game
size_t PGN::write_game (const Game &game)
{
    // TODO::
    ::std::string pgn = game.pgn ();
    (*this) << pgn;

    return 0;
}

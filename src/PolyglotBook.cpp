#include "PolyglotBook.h"
#include <algorithm>
#include <vector>
#include <iomanip>

//#include "Position.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "manipulator.h"

using namespace MoveGenerator;

#define POSITION(x) ((SIZE_PGHEADER) + (x)*(SIZE_PGENTRY))

#pragma region PolyglotEntry Operators

inline bool operator== (const PolyglotBook::PolyglotEntry& pe1, const PolyglotBook::PolyglotEntry& pe2)
{
    return 
        (pe1.key == pe2.key) &&
        (pe1.move == pe2.move) &&
        (pe1.weight == pe2.weight);
}

inline bool operator!= (const PolyglotBook::PolyglotEntry& pe1, const PolyglotBook::PolyglotEntry& pe2)
{
    return !(pe1 == pe2);
}

inline bool operator> (const PolyglotBook::PolyglotEntry& pe1, const PolyglotBook::PolyglotEntry& pe2)
{
    return 
        (pe1.key != pe2.key) ?
        (pe1.key > pe2.key) :
    (pe1.move > pe2.move);      // order by move value
    //(pe1.weight > pe2.weight);  // order by weight value
}

inline bool operator< (const PolyglotBook::PolyglotEntry& pe1, const PolyglotBook::PolyglotEntry& pe2)
{
    return
        (pe1.key != pe2.key) ?
        (pe1.key < pe2.key) :
    (pe1.move < pe2.move);      // order by move value
    //(pe1.weight < pe2.weight);  // order by weight value
}

inline bool operator>= (const PolyglotBook::PolyglotEntry& pe1, const PolyglotBook::PolyglotEntry& pe2)
{
    return
        (pe1.key != pe2.key) ?
        (pe1.key >= pe2.key) :
    (pe1.move >= pe2.move);      // order by move value
    //(pe1.weight >= pe2.weight);  // order by weight value
}

inline bool operator<= (const PolyglotBook::PolyglotEntry& pe1, const PolyglotBook::PolyglotEntry& pe2)
{
    return
        (pe1.key != pe2.key) ?
        (pe1.key <= pe2.key) :
    (pe1.move <= pe2.move);      // order by move value
    //(pe1.weight <= pe2.weight);  // order by weight value
}

PolyglotBook::PolyglotEntry::operator ::std::string () const
{
    ::std::ostringstream spe;

    Move m = Move (move);
    PType pt = PType ((m >> 12) & 0x7);
    // Set new type for promotion piece
    if (pt) prom_type (m, pt);

    spe << ::std::setfill ('0')
        << " key: " << ::std::setw (16) << ::std::hex << ::std::uppercase << key
        << ::std::setfill ('.')
        << " move: " << ::std::setw (5) << ::std::left << move_to_can (m)
        << ::std::setfill ('0')
        << " weight: " << ::std::setw (4) << ::std::right << ::std::dec << weight
        << " learn: " << ::std::setw (2) << learn;

    return spe.str ();
}


#pragma endregion

template<class T>
PolyglotBook& PolyglotBook::operator>> (T &n)
{
    n = T ();
    for (size_t i = 0; i < sizeof (T) && good (); ++i)
    {
        uint8_t byte = get ();
        n = T ((n << 8) + byte);
    }
    return *this;
}
template<>
PolyglotBook& PolyglotBook::operator>> (PolyglotEntry &pe)
{
    *this >> pe.key >> pe.move >> pe.weight >> pe.learn;
    return *this;
}

PolyglotBook::PolyglotBook()
    : ::std::fstream ()
    , _fn_book ("")
    , _mode (0)
    , _size_book (0)
    , _rkiss ()
{}
PolyglotBook::PolyglotBook (const          char *fn_book, ::std::ios_base::openmode mode)
    : ::std::fstream (fn_book, mode | ios_base::binary)
    , _fn_book (fn_book)
    , _mode (mode)
    , _size_book (0)
    , _rkiss ()
{}
PolyglotBook::PolyglotBook (const ::std::string &fn_book, ::std::ios_base::openmode mode)
    : ::std::fstream (fn_book, mode | ios_base::binary)
    , _fn_book (fn_book)
    , _mode (mode)
    , _size_book (0)
    , _rkiss ()
{}

PolyglotBook::~PolyglotBook ()
{
    close ();
}

// open the file in mode
// Read -> ios_base::in
// Write-> ios_base::out
bool PolyglotBook::open (const          char *fn_book, ::std::ios_base::openmode mode)
{
    close ();
    ::std::fstream::open (fn_book, mode | ::std::ios_base::binary);
    clear (); // Reset any error flag to allow retry open()
    _fn_book = fn_book;
    _mode    = mode;
    return is_open ();
}
bool PolyglotBook::open (const ::std::string &fn_book, ::std::ios_base::openmode mode)
{
    close ();
    ::std::fstream::open (fn_book, mode | ::std::ios_base::binary);
    clear (); // Reset any error flag to allow retry open()
    _fn_book = fn_book;
    _mode    = mode;
    return is_open ();
}

void PolyglotBook::close () { if (is_open ()) ::std::fstream::close (); }

size_t PolyglotBook::find_index (const Key key)
{
    if (!is_open ()) return ERR_INDEX;

    size_t beg = size_t (0);
    size_t end = size_t ((size () - SIZE_PGHEADER) / SIZE_PGENTRY - 1);

    PolyglotEntry pe;

    ASSERT (beg <= end);

    if (beg == end)
    {
        seekg (POSITION (beg));
        *this >> pe;
        if (key != pe.key) return ERR_INDEX;
    }
    else
    {
        while (beg < end && good ())
        {
            size_t mid = (beg + end) / 2;
            ASSERT (mid >= beg && mid < end);

            seekg (POSITION (mid));

            *this >> pe;
            if (key <= pe.key)
            {
                end = mid;
            }
            else
            {
                beg = mid + 1;
            }
        }
    }
    ASSERT (beg == end);
    return beg;
}
size_t PolyglotBook::find_index (const Position &pos)
{
    return find_index (ZobPG.key_posi (pos));
}
size_t PolyglotBook::find_index (const        char *fen, bool c960)
{
    return find_index (ZobPG.key_fen (fen, c960));
}
size_t PolyglotBook::find_index (const ::std::string &fen, bool c960)
{
    return find_index (ZobPG.key_fen (fen, c960));
}

Move PolyglotBook::probe_move (const Position &pos, bool pick_best)
{
    if (!is_open () || !(_mode & ::std::ios_base::in)) return MOVE_NONE;

    Key key = ZobPG.key_posi (pos);

    size_t index = find_index (key);
    if (ERR_INDEX == index) return MOVE_NONE;

    seekg (POSITION (index));

    Move move = MOVE_NONE;

    PolyglotEntry pe;

    uint16_t max_weight = 0;
    uint32_t sum_weight = 0;

    //::std::vector<PolyglotEntry> lst_pe;
    //while ((*this >> pe), (pe.key == key) && good ())
    //{
    //    lst_pe.emplace_back (pe);
    //    max_weight = ::std::max (max_weight, pe.weight);
    //    sum_weight += pe.weight;
    //}
    //if (!lst_pe.size ()) return MOVE_NONE;
    //
    //if (pick_best)
    //{
    //    ::std::vector<PolyglotEntry>::const_iterator itr = lst_pe.cbegin ();
    //    while (itr != lst_pe.cend ())
    //    {
    //        pe = *itr;
    //        if (pe.weight == max_weight)
    //        {
    //            move = Move (pe.move);
    //            break;
    //        }
    //        ++itr;
    //    }
    //}
    //else
    //{
    //    //There is a straightforward algorithm for picking an item at random, where items have individual weights:
    //    //1) calculate the sum of all the weights
    //    //2) pick a random number that is 0 or greater and is less than the sum of the weights
    //    //3) go through the items one at a time, subtracting their weight from your random number, until you get the item where the random number is less than that item's weight
    //
    //    uint32_t rand = (_rkiss.randX<uint32_t> () % sum_weight);
    //    ::std::vector<PolyglotEntry>::const_iterator itr = lst_pe.cbegin ();
    //    while (itr != lst_pe.cend ())
    //    {
    //        pe = *itr;
    //        if (pe.weight > rand)
    //        {
    //            move = Move (pe.move);
    //            break;
    //        }
    //        rand -= pe.weight;
    //        ++itr;
    //    }
    //}

    while ((*this >> pe), (pe.key == key) && good ())
    {
        max_weight = ::std::max (max_weight, pe.weight);
        sum_weight += pe.weight;

        // Choose book move according to its score.
        // If a move has a high score it has higher probability
        // to be choosen than a move with lower score.
        // Note that first entry is always chosen.

        //uint32_t rand = _rkiss.randX<uint32_t> ();
        //if ((sum_weight && rand % sum_weight < pe.weight) ||
        //    (pick_best && (pe.weight == max_weight)))
        //{
        //    move = Move (pe.move);
        //}

        if (pick_best)
        {
            if (pe.weight == max_weight) move = Move (pe.move);
        }
        else if (sum_weight)
        {
            uint32_t rand = (_rkiss.randX<uint32_t> () % sum_weight);
            if (pe.weight > rand) move = Move (pe.move);
        }
        else // if not pick best and sum of weight = 0
        {
            move = Move (pe.move);
        }
    }

    if (MOVE_NONE == move) return MOVE_NONE;

    // A PolyGlot book move is encoded as follows:
    //
    // bit 00-05: destiny square    (0...63)
    // bit 06-11: origin square     (0...63)
    // bit 12-14: promotion piece   (NONE = 0, KNIGHT = 1 ... QUEEN = 4)
    // bit    15: empty
    // Move is "0" (a1a1) then it should simply be ignored.
    // It seems to me that in that case one might as well delete the entry from the book.

    // Castling moves follow "king captures rook" representation.
    // Promotion moves have promotion piece different then our structure of move
    // So in case book move is a promotion we have to convert to our representation,
    // in all the other cases we can directly compare with a Move after having masked out
    // the special Move's flags (bit 14-15) that are not supported by PolyGlot.
    // Polyglot use 3 bits while we use 2 bits
    PType pt = PType ((move >> 12) & 0x7);
    // Set new type for promotion piece
    if (pt) prom_type (move, pt);

    // Add 'special move' flags and verify it is legal
    MoveList lst_move = generate<LEGAL> (pos);
    MoveList::const_iterator itr = lst_move.cbegin ();
    while (itr != lst_move.cend ())
    {
        Move m = *itr;
        //if ((m ^ _mtype (m)) == move)
        if ((m & 0x3FFF) == move)
        {
            return m;
        }
        ++itr;
    }

    return MOVE_NONE;
}

::std::string PolyglotBook::read_entries (const Position &pos)
{
    if (!is_open () || !(_mode & ::std::ios_base::in)) return "";

    Key key = ZobPG.key_posi (pos);

    size_t index = find_index (key);
    if (ERR_INDEX == index)
    {
        ::std::cerr << "ERROR: no such key... "
            << ::std::hex << ::std::uppercase << key << ::std::endl;
        return "";
    }

    seekg (POSITION (index));

    PolyglotEntry pe;
    ::std::vector<PolyglotEntry> lst_pe;

    uint32_t sum_weight = 0;
    while ((*this >> pe), (pe.key == key) && good ())
    {
        lst_pe.emplace_back (pe);
        sum_weight += pe.weight;
    }

    ::std::ostringstream sread;

    ::std::vector<PolyglotEntry>::const_iterator itr = lst_pe.cbegin ();
    while (itr != lst_pe.cend ())
    {
        pe = *itr;
        sread << ::std::setfill ('0')
            << pe << " prob: " << ::std::right << ::std::fixed << ::std::width_prec (6, 2)
            << (sum_weight ? double (pe.weight) * 100 / double (sum_weight) : 0.0) << ::std::endl;

        ++itr;
    }

    return sread.str ();
}

void PolyglotBook::insert_entry (const PolyglotBook::PolyglotEntry &pe)
{
    if (!is_open () || !(_mode & ::std::ios_base::out)) return;

    size_t index = find_index (pe.key);
    if (ERR_INDEX == index)
    {

    }
    else
    {
        // move found
        if (true)
        {
            // do nothing
        }
        else
        {

        }
    }


}

void PolyglotBook::import_pgn (const ::std::string &fn_pgn)
{

}

void PolyglotBook::merge_book (const ::std::string &fn_book)
{

}

//void PolyglotBook::dump ()
//{
//
//}
//
//void PolyglotBook::info ()
//{
//
//}



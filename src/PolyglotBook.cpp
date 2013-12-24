#include "PolyglotBook.h"
#include <algorithm>
#include <vector>
#include <iomanip>

//#include "Position.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "manipulator.h"

using namespace std;
using namespace MoveGenerator;

#define STM_POS(x)  ((SIZE_PGHEADER) + (x)*(SIZE_PGENTRY))

#pragma region PolyglotEntry Operators

inline bool operator== (const PolyglotBook::PolyglotEntry &pe1, const PolyglotBook::PolyglotEntry &pe2)
{
    return 
        (pe1.key == pe2.key) &&
        (pe1.move == pe2.move) &&
        (pe1.weight == pe2.weight);
}

inline bool operator!= (const PolyglotBook::PolyglotEntry &pe1, const PolyglotBook::PolyglotEntry &pe2)
{
    return !(pe1 == pe2);
}

inline bool operator> (const PolyglotBook::PolyglotEntry &pe1, const PolyglotBook::PolyglotEntry &pe2)
{
    return 
        (pe1.key != pe2.key) ?
        (pe1.key > pe2.key) :
    (pe1.move > pe2.move);      // order by move value
    //(pe1.weight > pe2.weight);  // order by weight value
}

inline bool operator< (const PolyglotBook::PolyglotEntry &pe1, const PolyglotBook::PolyglotEntry &pe2)
{
    return
        (pe1.key != pe2.key) ?
        (pe1.key < pe2.key) :
    (pe1.move < pe2.move);      // order by move value
    //(pe1.weight < pe2.weight);  // order by weight value
}

inline bool operator>= (const PolyglotBook::PolyglotEntry &pe1, const PolyglotBook::PolyglotEntry &pe2)
{
    return
        (pe1.key != pe2.key) ?
        (pe1.key >= pe2.key) :
    (pe1.move >= pe2.move);      // order by move value
    //(pe1.weight >= pe2.weight);  // order by weight value
}

inline bool operator<= (const PolyglotBook::PolyglotEntry &pe1, const PolyglotBook::PolyglotEntry &pe2)
{
    return
        (pe1.key != pe2.key) ?
        (pe1.key <= pe2.key) :
    (pe1.move <= pe2.move);      // order by move value
    //(pe1.weight <= pe2.weight);  // order by weight value
}

PolyglotBook::PolyglotEntry::operator string () const
{
    ostringstream spe;

    Move m = Move (move);
    PType pt = PType ((m >> 12) & 0x7);
    // Set new type for promotion piece
    if (pt) prom_type (m, pt);

    spe << setfill ('0')
        << " key: " << setw (16) << hex << uppercase << key
        << setfill ('.')
        << " move: " << setw (5) << left << move_to_can (m)
        << setfill ('0')
        << " weight: " << setw (4) << right << dec << weight
        << " learn: " << setw (2) << learn;

    return spe.str ();
}


#pragma endregion

template<class T>
PolyglotBook& PolyglotBook::operator>> (T &t)
{
    t = T ();
    for (size_t i = 0; i < sizeof (T) && good (); ++i)
    {
        uint8_t byte = get ();
        t = T ((t << 8) + byte);
    }
    return *this;
}
template<>
PolyglotBook& PolyglotBook::operator>> (PolyglotEntry &pe)
{
    *this >> pe.key >> pe.move >> pe.weight >> pe.learn;
    return *this;
}

template<class T>
PolyglotBook& PolyglotBook::operator<< (T &t)
{
    size_t size = sizeof (T);
    for (size_t i = 0; i < size && good (); ++i)
    {
        uint8_t byte = uint8_t (t >> (8*(size - 1 - i)));
        put (byte);
    }
    return *this;
}
template<>
PolyglotBook& PolyglotBook::operator<< (PolyglotEntry &pe)
{
    *this << pe.key << pe.move << pe.weight << pe.learn;
    return *this;
}

PolyglotBook::PolyglotBook()
    : fstream ()
    , _fn_book ("")
    , _mode (0)
    , _size_book (0)
    , _rkiss ()
{}
PolyglotBook::PolyglotBook (const        char *fn_book, ios_base::openmode mode)
    : fstream (fn_book, mode | ios_base::binary)
    , _fn_book (fn_book)
    , _mode (mode)
    , _size_book (0)
    , _rkiss ()
{}
PolyglotBook::PolyglotBook (const string &fn_book, ios_base::openmode mode)
    : fstream (fn_book, mode | ios_base::binary)
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
bool PolyglotBook::open (const        char *fn_book, ios_base::openmode mode)
{
    close ();
    fstream::open (fn_book, mode | ios_base::binary);
    clear (); // Reset any error flag to allow retry open()
    _fn_book = fn_book;
    _mode    = mode;
    return fstream::is_open ();
}
bool PolyglotBook::open (const string &fn_book, ios_base::openmode mode)
{
    close ();
    fstream::open (fn_book, mode | ios_base::binary);
    clear (); // Reset any error flag to allow retry open()
    _fn_book = fn_book;
    _mode    = mode;
    return fstream::is_open ();
}

void PolyglotBook::close () { if (fstream::is_open ()) fstream::close (); }

size_t PolyglotBook::find_index (const Key key)
{
    if (!fstream::is_open ()) return ERROR_INDEX;

    size_t beg = size_t (0);
    size_t end = size_t ((size () - SIZE_PGHEADER) / SIZE_PGENTRY - 1);

    PolyglotEntry pe;

    ASSERT (beg <= end);

    if (beg == end)
    {
        seekg (STM_POS (beg));
        *this >> pe;
    }
    else
    {
        while (beg < end && good ())
        {
            size_t mid = (beg + end) / 2;
            ASSERT (mid >= beg && mid < end);

            seekg (STM_POS (mid));

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
    return (key == pe.key) ? beg : ERROR_INDEX;
}
size_t PolyglotBook::find_index (const Position &pos)
{
    return find_index (ZobPG.compute_posi_key (pos));
}
size_t PolyglotBook::find_index (const        char *fen, bool c960)
{
    return find_index (ZobPG.compute_fen_key (fen, c960));
}
size_t PolyglotBook::find_index (const string &fen, bool c960)
{
    return find_index (ZobPG.compute_fen_key (fen, c960));
}

Move PolyglotBook::probe_move (const Position &pos, bool pick_best)
{
    if (!fstream::is_open () || !(_mode & ios_base::in))
    {
        if (!open (_fn_book, ios_base::in)) return MOVE_NONE;
    }

    Key key = ZobPG.compute_posi_key (pos);

    size_t index = find_index (key);
    if (ERROR_INDEX == index) return MOVE_NONE;

    seekg (STM_POS (index));

    Move move = MOVE_NONE;

    PolyglotEntry pe;

    uint16_t max_weight = 0;
    uint32_t sum_weight = 0;

    //vector<PolyglotEntry> pe_list;
    //while ((*this >> pe), (pe.key == key) && good ())
    //{
    //    pe_list.emplace_back (pe);
    //    max_weight = max (max_weight, pe.weight);
    //    sum_weight += pe.weight;
    //}
    //if (!pe_list.size ()) return MOVE_NONE;
    //
    //if (pick_best)
    //{
    //    vector<PolyglotEntry>::const_iterator itr = pe_list.cbegin ();
    //    while (itr != pe_list.cend ())
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
    //    vector<PolyglotEntry>::const_iterator itr = pe_list.cbegin ();
    //    while (itr != pe_list.cend ())
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
        if (0 == pe.move) continue;

        max_weight = max (max_weight, pe.weight);
        sum_weight += pe.weight;

        // Choose book move according to its score.
        // If a move has a very high score it has a higher probability
        // of being choosen than a move with a lower score.
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
            uint16_t rand = _rkiss.randX<uint16_t> () % sum_weight;
            if (pe.weight > rand) move = Move (pe.move);
        }
        else if (MOVE_NONE == move) // if not pick best and sum of weight = 0
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
    MoveList mov_lst = generate<LEGAL> (pos);
    for (MoveList::const_iterator itr = mov_lst.cbegin (); itr != mov_lst.cend (); ++itr)
    {
        Move m = *itr;
        //if ((m ^ m_type (m)) == move)
        if ((m & 0x3FFF) == move)
        {
            return m;
        }
    }

    return MOVE_NONE;
}

string PolyglotBook::read_entries (const Position &pos)
{
    if (!fstream::is_open () || !(_mode & ios_base::in)) return "";

    Key key = ZobPG.compute_posi_key (pos);

    size_t index = find_index (key);
    if (ERROR_INDEX == index)
    {
        cerr << "ERROR: no such key... "
            << hex << uppercase << key << endl;
        return "";
    }

    seekg (STM_POS (index));

    PolyglotEntry pe;
    vector<PolyglotEntry> pe_list;

    uint32_t sum_weight = 0;
    while ((*this >> pe), (pe.key == key) && good ())
    {
        pe_list.emplace_back (pe);
        sum_weight += pe.weight;
    }

    ostringstream sread;
    for_each (pe_list.cbegin (), pe_list.cend (), [&sread, &sum_weight] (PolyglotEntry pe)
    {
        sread << setfill ('0')
            << pe << " prob: " << right << fixed << width_prec (6, 2)
            << (sum_weight ? double (pe.weight) * 100 / double (sum_weight) : 0.0) << endl;
    });

    return sread.str ();
}

void PolyglotBook::insert_entry (const PolyglotBook::PolyglotEntry &pe)
{
    if (!fstream::is_open () || !(_mode & ios_base::out)) return;

    size_t index = find_index (pe.key);
    if (ERROR_INDEX == index)
    {

    }
    else
    {
        // move found
        if (true)
        {
            // do nothing (UPDATE)
        }
        else
        {

        }
    }


}

void PolyglotBook::import_pgn (const string &fn_pgn)
{

}

void PolyglotBook::merge_book (const string &fn_book)
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

#include "PolyglotBook.h"

#include <iomanip>

#include "manipulator.h"
#include "Position.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include "Notation.h"

namespace OpeningBook {

    using namespace std;
    using namespace MoveGen;
    using namespace Notation;

    #define STM_POS(x)  (streampos)(u64(HeaderSize) + (x)*u64(EntrySize))

    const streampos PolyglotBook::EntrySize  = sizeof (Entry);
    const streampos PolyglotBook::HeaderSize = 0*EntrySize;
    const streampos PolyglotBook::ErrorIndex   = streampos(-1);

    inline bool operator== (const PolyglotBook::Entry &pe1, const PolyglotBook::Entry &pe2)
    {
        return (pe1.key == pe2.key)
            && (pe1.move == pe2.move)
            && (pe1.weight == pe2.weight);
    }

    inline bool operator!= (const PolyglotBook::Entry &pe1, const PolyglotBook::Entry &pe2)
    {
        return !(pe1 == pe2);
    }

    inline bool operator>  (const PolyglotBook::Entry &pe1, const PolyglotBook::Entry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key > pe2.key) :
                //(pe1.move > pe2.move);      // order by move value
                (pe1.weight > pe2.weight);  // order by weight value
    }

    inline bool operator<  (const PolyglotBook::Entry &pe1, const PolyglotBook::Entry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key < pe2.key) :
                //(pe1.move < pe2.move);      // order by move value
                (pe1.weight < pe2.weight);  // order by weight value
    }

    inline bool operator>= (const PolyglotBook::Entry &pe1, const PolyglotBook::Entry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key >= pe2.key) :
                //(pe1.move >= pe2.move);      // order by move value
                (pe1.weight >= pe2.weight);  // order by weight value
    }

    inline bool operator<= (const PolyglotBook::Entry &pe1, const PolyglotBook::Entry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key <= pe2.key) :
                //(pe1.move <= pe2.move);      // order by move value
                (pe1.weight <= pe2.weight);  // order by weight value
    }

    PolyglotBook::Entry::operator string () const
    {
        ostringstream oss;

        Move m = Move(move);
        // Set new type for promotion piece
        PieceT pt = PieceT((m >> 12) & TOTL);
        if (pt != PAWN) promote (m, pt);

        oss << setfill ('0')
            << " key: "    << setw (16) << hex << uppercase << key
            << setfill (' ')
            << " move: "   << setw (5) << left << move_to_can (m)
            << setfill ('0')
            << " weight: " << setw (4) << right << dec << weight
            << " learn: "  << setw (2) << learn
            << setfill (' ');

        return oss.str ();
    }


    template<class T>
    PolyglotBook& PolyglotBook::operator>> (T &t)
    {
        t = T ();
        for (u08 i = 0; i < sizeof (t) && good (); ++i)
        {
            u08 byte = u08(get ());
            t = T ((t << 8) + byte);
        }
        return *this;
    }
    template<>
    PolyglotBook& PolyglotBook::operator>> (Entry &pbe)
    {
        *this >> pbe.key >> pbe.move >> pbe.weight >> pbe.learn;
        return *this;
    }

    template<class T>
    PolyglotBook& PolyglotBook::operator<< (T &t)
    {
        for (u08 i = 0; i < sizeof (t) && good (); ++i)
        {
            u08 byte = u08(t >> (8*(sizeof (t) - 1 - i)));
            put (byte);
        }
        return *this;
    }
    template<>
    PolyglotBook& PolyglotBook::operator<< (Entry &pbe)
    {
        *this << pbe.key << pbe.move << pbe.weight << pbe.learn;
        return *this;
    }

    PolyglotBook::PolyglotBook ()
        : fstream ()
        , _book_fn ("")
        , _mode (openmode (0))
        , _size_book (0)
    {}

    PolyglotBook::PolyglotBook (const string &book_fn, openmode mode)
        : fstream (book_fn.c_str (), mode|binary)
        , _book_fn (book_fn)
        , _mode (mode)
        , _size_book (0)
    {}

    PolyglotBook::~PolyglotBook ()
    {
        close ();
    }

    // open() tries to open a book file with the given name after closing any existing one.
    // mode:
    // Read -> ios_base::in
    // Write-> ios_base::out
    bool PolyglotBook::open (const string &book_fn, openmode mode)
    {
        close ();
        fstream::open (book_fn.c_str (), mode|binary);
        clear (); // Reset any error flag to allow retry open()
        _book_fn = book_fn;
        _mode    = mode;
        return is_open ();
    }

    streampos PolyglotBook::find_index (const Key key)
    {
        if (!is_open ()) return ErrorIndex;

        streampos beg = streampos(0);
        streampos end = streampos((size () - HeaderSize) / EntrySize - 1);

        Entry pbe;

        assert (beg <= end);

        if (beg == end)
        {
            seekg (STM_POS (beg));
            *this >> pbe;
        }
        else
        {
            while (beg < end && good ())
            {
                streampos mid = (beg + end) / 2;
                assert (mid >= beg && mid < end);

                seekg (STM_POS (mid));

                *this >> pbe;
                if (key <= pbe.key)
                {
                    end = mid;
                }
                else
                {
                    beg = mid + streampos(1);
                }
            }

            assert (beg == end);
        }
        
        return (key == pbe.key) ? beg : ErrorIndex;
    }
    streampos PolyglotBook::find_index (const Position &pos)
    {
        return find_index (ZobPG.compute_posi_key (pos));
    }

    streampos PolyglotBook::find_index (const string &fen, bool c960)
    {
        return find_index (ZobPG.compute_fen_key (fen, c960));
    }

    Move PolyglotBook::probe_move (const Position &pos, bool pick_best)
    {
        //if (!is_open () || !(_mode & ios_base::in))
        //{
        //    if (!open (_book_fn, ios_base::in)) return MOVE_NONE;
        //}

        Key key = ZobPG.compute_posi_key (pos);

        streampos index = find_index (key);
        if (ErrorIndex == index) return MOVE_NONE;

        seekg (STM_POS (index));

        Move move = MOVE_NONE;

        Entry pbe;

        u16 max_weight = 0;
        u32 weight_sum = 0;

        //vector<Entry> pe_list;
        //while ((*this >> pbe), (pbe.key == key))
        //{
        //    pe_list.push_back (pbe);
        //    max_weight = max (max_weight, pbe.weight);
        //    weight_sum += pbe.weight;
        //}
        //if (!pe_list.size ()) return MOVE_NONE;
        //
        //if (pick_best)
        //{
        //    vector<Entry>::const_iterator ms = pe_list.begin ();
        //    while (ms != pe_list.end ())
        //    {
        //        pbe = *ms;
        //        if (pbe.weight == max_weight)
        //        {
        //            move = Move(pbe.move);
        //            break;
        //        }
        //        ++ms;
        //    }
        //}
        //else
        //{
        //    //There is a straightforward algorithm for picking an item at random, where items have individual weights:
        //    //1) calculate the sum of all the weights
        //    //2) pick a random number that is 0 or greater and is less than the sum of the weights
        //    //3) go through the items one at a time, subtracting their weight from your random number, until you get the item where the random number is less than that item's weight
        //
        //    u32 rand = (_rkiss.rand<u32> () % weight_sum);
        //    vector<Entry>::const_iterator ms = pe_list.begin ();
        //    while (ms != pe_list.end ())
        //    {
        //        pbe = *ms;
        //        if (pbe.weight > rand)
        //        {
        //            move = Move(pbe.move);
        //            break;
        //        }
        //        rand -= pbe.weight;
        //        ++ms;
        //    }
        //}

        while ((*this >> pbe), (pbe.key == key))
        {
            if (MOVE_NONE == pbe.move) continue;

            max_weight = max (max_weight, pbe.weight);
            weight_sum += pbe.weight;

            if (pick_best)
            {
                if (pbe.weight == max_weight) move = Move(pbe.move);
            }
            // Choose book move according to its score.
            // If a move has a very high score it has a higher probability
            // of being choosen than a move with a lower score.
            else
            if (weight_sum != 0)
            {
                u16 rand = _rkiss.rand<u16> () % weight_sum;
                if (pbe.weight > rand) move = Move(pbe.move);
            }
            // Note that first entry is always chosen if not pick best and sum of weight = 0
            else
            if (MOVE_NONE == move)
            {
                move = Move(pbe.move);
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
        // So in case book move is a promotion have to convert to our representation,
        // in all the other cases can directly compare with a Move after having masked out
        // the special Move's flags (bit 14-15) that are not supported by PolyGlot.
        // Polyglot use 3 bits while use 2 bits
        PieceT pt = PieceT((move >> 12) & TOTL);
        // Set new type for promotion piece
        if (pt != PAWN) promote (move, pt);

        // Add 'special move' flags and verify it is legal
        for (MoveList<LEGAL> ms (pos); *ms != MOVE_NONE; ++ms)
        {
            Move m = *ms;
            if ((m & ~PROMOTE) == move)
            {
                return m;
            }
        }

        return MOVE_NONE;
    }

    string PolyglotBook::read_entries (const Position &pos)
    {
        if (!is_open () || !(_mode & in)) return "";

        Key key = ZobPG.compute_posi_key (pos);

        streampos index = find_index (key);
        if (ErrorIndex == index)
        {
            cerr << "ERROR: no such key... "
                << hex << uppercase << key
                << endl;
            return "";
        }

        seekg (STM_POS (index));

        Entry pbe;

        vector<Entry> pe_list;

        u32 weight_sum = 0;
        while ((*this >> pbe), (pbe.key == key))
        {
            pe_list.push_back (pbe);
            weight_sum += pbe.weight;
        }

        //TODO::
        ostringstream oss;
        //for_each (pe_list.begin (), pe_list.end (), [&oss, &weight_sum] (Entry _pbe)
        //{
        //    oss << setfill ('0')
        //        << _pbe << " prob: " << right << fixed << width_prec (6, 2)
        //        << (weight_sum ? 100 * (float) _pbe.weight / weight_sum : 0.0)
        //        << setfill (' ') << endl;
        //});

        return oss.str ();
    }

    void PolyglotBook::insert_entry (const Entry &pbe)
    {
        if (!is_open () || !(_mode & out)) return;

        streampos index = find_index (pbe.key);
        if (ErrorIndex == index)
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

    //void PolyglotBook::import_pgn (const string &pgn_fn)
    //{
    //    (void) pgn_fn;
    //}
    //
    //void PolyglotBook::merge_book (const string &book_fn)
    //{
    //    (void) book_fn;
    //
    //}

    //void PolyglotBook::dump ()
    //{
    //
    //}
    //
    //void PolyglotBook::info ()
    //{
    //
    //}

}

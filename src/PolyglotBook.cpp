#include "PolyglotBook.h"

#include <iomanip>

#include "manipulator.h"
#include "Position.h"
#include "PRNG.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include "Notation.h"

namespace OpeningBook  {

    using namespace std;
    using namespace MoveGen;
    using namespace Notation;

    #define OFFSET(x)  HeaderSize + (x)*EntrySize

    const size_t PolyglotBook::EntrySize  = sizeof (PBEntry);
    const size_t PolyglotBook::HeaderSize = 0*EntrySize;

    bool operator== (const PolyglotBook::PBEntry &pe1, const PolyglotBook::PBEntry &pe2)
    {
        return (pe1.key == pe2.key)
            && (pe1.move == pe2.move)
            && (pe1.weight == pe2.weight);
    }

    bool operator!= (const PolyglotBook::PBEntry &pe1, const PolyglotBook::PBEntry &pe2)
    {
        return !(pe1 == pe2);
    }

    bool operator>  (const PolyglotBook::PBEntry &pe1, const PolyglotBook::PBEntry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key > pe2.key) :
                //(pe1.move > pe2.move);      // order by move value
                (pe1.weight > pe2.weight);  // order by weight value
    }

    bool operator<  (const PolyglotBook::PBEntry &pe1, const PolyglotBook::PBEntry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key < pe2.key) :
                //(pe1.move < pe2.move);      // order by move value
                (pe1.weight < pe2.weight);  // order by weight value
    }

    bool operator>= (const PolyglotBook::PBEntry &pe1, const PolyglotBook::PBEntry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key >= pe2.key) :
                //(pe1.move >= pe2.move);      // order by move value
                (pe1.weight >= pe2.weight);  // order by weight value
    }

    bool operator<= (const PolyglotBook::PBEntry &pe1, const PolyglotBook::PBEntry &pe2)
    {
        return (pe1.key != pe2.key) ?
                (pe1.key <= pe2.key) :
                //(pe1.move <= pe2.move);      // order by move value
                (pe1.weight <= pe2.weight);  // order by weight value
    }

    PolyglotBook::PBEntry::operator string () const
    {
        ostringstream oss;

        auto m = Move(move);
        // Set new type for promotion piece
        auto pt = PieceT((m >> 12) & TOTL);
        if (pt != PAWN) promote (m, pt);

        oss << " key: "    << std::setw (16) << std::setfill ('0') << std::hex << std::uppercase << key << std::nouppercase
            << " move: "   << std::setw ( 5) << std::setfill (' ') << std::left << move_to_can (m) << std::right
            << " weight: " << std::setw ( 4) << std::setfill ('0') << std::dec << weight
            << " learn: "  << std::setw ( 2) << std::setfill ('0') << std::dec << learn
            << std::setfill (' ');

        return oss.str ();
    }

    template<class T>
    PolyglotBook& PolyglotBook::operator>> (T &t)
    {
        t = T();
        for (u08 i = 0; i < sizeof (t) && good (); ++i)
        {
            u08 byte = u08(get ());
            t = T((t << 8) + byte);
        }
        return *this;
    }
    template<>
    PolyglotBook& PolyglotBook::operator>> (PBEntry &pbe)
    {
        *this >> pbe.key
              >> pbe.move
              >> pbe.weight
              >> pbe.learn;
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
    PolyglotBook& PolyglotBook::operator<< (PBEntry &pbe)
    {
        *this << pbe.key
              << pbe.move
              << pbe.weight
              << pbe.learn;
        return *this;
    }

    PolyglotBook::PolyglotBook ()
        : fstream ()
        , _book_fn ("")
        , _mode (openmode(0))
        , _size (0)
    {}

    PolyglotBook::PolyglotBook (const string &book_fn, openmode mode)
        : fstream (book_fn, mode|ios_base::binary)
        , _book_fn (book_fn)
        , _mode (mode)
        , _size (0)
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
        fstream::open (book_fn, mode|ios_base::binary);
        clear (); // Reset any error flag to allow retry open()
        _book_fn = book_fn;
        _mode    = mode;
        return is_open ();
    }

    size_t PolyglotBook::find_index (const Key key)
    {
        if (!is_open ()) return streampos(-1);

        auto beg_index = size_t(0);
        auto end_index = size_t((size () - HeaderSize) / EntrySize - 1);

        PBEntry pbe;

        assert (beg_index <= end_index);
        while (beg_index < end_index && good ())
        {
            auto mid_index = size_t((beg_index + end_index) / 2);
            assert (mid_index >= beg_index && mid_index < end_index);

            seekg (OFFSET (mid_index), ios_base::beg);
            *this >> pbe;

            if (key <= pbe.key)
            {
                end_index = mid_index;
            }
            else
            {
                beg_index = mid_index + 1;
            }
        }
        assert (beg_index == end_index);

        return beg_index;
    }

    size_t PolyglotBook::find_index (const Position &pos)
    {
        return find_index (pos.posi_key ());
    }

    size_t PolyglotBook::find_index (const string &fen, bool c960)
    {
        return find_index (Position (fen, nullptr, c960).posi_key ());
    }

    Move PolyglotBook::probe_move (const Position &pos, bool pick_best)
    {
        static PRNG pr (now ());

        Key key = pos.posi_key ();

        auto index = find_index (key);

        seekg (OFFSET (index));

        auto move = MOVE_NONE;

        PBEntry pbe;

        u16 max_weight = 0;
        u32 weight_sum = 0;

        //vector<PBEntry> pbes;
        //while ((*this >> pbe), (pbe.key == key))
        //{
        //    pbes.push_back (pbe);
        //    max_weight = max (max_weight, pbe.weight);
        //    weight_sum += pbe.weight;
        //}
        //if (!pbes.size ()) return MOVE_NONE;
        //
        //if (pick_best)
        //{
        //    vector<PBEntry>::const_iterator ms = pbes.begin ();
        //    while (ms != pbes.end ())
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
        //    u32 rand = (pr.rand<u32> () % weight_sum);
        //    vector<PBEntry>::const_iterator ms = pbes.begin ();
        //    while (ms != pbes.end ())
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
                u16 rand = pr.rand<u16> () % weight_sum;
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
        auto pt = PieceT((move >> 12) & TOTL);
        // Set new type for promotion piece
        if (pt != PAWN) promote (move, pt);

        // Add special move flags and verify it is legal
        for (const auto &m : MoveList<LEGAL> (pos))
        {
            if ((m.move & ~PROMOTE) == move)
            {
                return m;
            }
        }

        return MOVE_NONE;
    }

    string PolyglotBook::read_entries (const Position &pos)
    {
        ostringstream oss;

        if (is_open () && (_mode & in))
        {
            Key key = pos.posi_key ();

            auto index = find_index (key);

            seekg (OFFSET (index));

            vector<PBEntry> pbes;
            PBEntry tmp_pbe;
            u32 weight_sum = 0;
            while ((*this >> tmp_pbe), (tmp_pbe.key == key))
            {
                pbes.push_back (tmp_pbe);
                weight_sum += tmp_pbe.weight;
            }
        
            if (pbes.size () == 0)
            {
                cerr << "ERROR: no such key... "
                    << std::hex << std::uppercase << key << std::nouppercase << std::dec
                    << endl;
            }
            else
            {
                for_each (pbes.begin (), pbes.end (), [&oss, &weight_sum] (PBEntry pbe)
                {
                    oss << pbe 
                        << " prob: " << std::setfill ('0') << std::fixed << std::width_prec (6, 2) << (weight_sum != 0 ? 100.0 * pbe.weight / weight_sum : 0.0) << std::setfill (' ')
                        << endl;
                });
            }
        }
        return oss.str ();
    }

}

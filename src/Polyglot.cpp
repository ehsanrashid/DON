#include "Polyglot.h"

#include <iomanip>

#include "Position.h"
#include "PRNG.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include "manipulator.h"
#include "Notation.h"

namespace Polyglot {

    using namespace std;
    using namespace MoveGen;
    using namespace Notation;

    #define OFFSET(x)  (Book::HeaderSize + (x)*Entry::Size)

    // -------------------------

    const u08 Entry::Size = sizeof (Entry);
    static_assert (Entry::Size == 16, "Incorrect Entry::size");

    const Entry Entry::NullEntry = { 0 , 0 , 0 , 0 };

    Entry::operator string () const
    {
        ostringstream oss;

        auto m = Move(move);
        // Set new type for promotion piece
        auto pt = PieceT((m >> 12) & TOTL);
        if (pt != PAWN) promote (m, pt);
        // TODO::Add special move flags and verify it is legal
        oss << " key: "    << std::setw (16) << std::setfill ('0') << std::hex << std::uppercase << key << std::nouppercase
            << " move: "   << std::setw ( 5) << std::setfill (' ') << std::left << move_to_can (m) << std::right
            << " weight: " << std::setw ( 4) << std::setfill ('0') << std::dec << weight
            << " learn: "  << std::setw ( 2) << std::setfill ('0') << std::dec << learn
            << std::setfill (' ');

        return oss.str ();
    }

    // -------------------------

    Book::Book (const string &book_fn, openmode mode)
        : fstream (book_fn, mode|ios_base::binary)
        , _book_fn (book_fn)
        , _mode (mode)
        , _size (0U)
    {}

    Book::~Book ()
    {
        _book_fn = "";
        _mode    = openmode (0);
        _size    = 0U;
        close ();
    }

    // open() tries to open a book file with the given name after closing any existing one.
    // mode:
    // Read -> ios_base::in
    // Write-> ios_base::out
    bool Book::open (const string &book_fn, openmode mode)
    {
        _book_fn = book_fn;
        _mode    = mode;
        _size    = 0U;
        fstream::open (book_fn, mode|ios_base::binary);
        clear (); // Reset any error flag to allow retry open()
        return is_open ();
    }
    void Book::close ()
    {
        if (is_open ())
        {
            std::fstream::close ();
        }
    }

    template<class T>
    Book& Book::operator>> (      T &t)
    {
        t = T ();
        for (u08 i = 0; i < sizeof (t) && good (); ++i)
        {
            u08 byte = u08(get ());
            t = T ((t << 8) + byte);
        }
        return *this;
    }
    template<class T>
    Book& Book::operator<< (const T &t)
    {
        for (u08 i = 0; i < sizeof (t) && good (); ++i)
        {
            u08 byte = u08(t >> (8*(sizeof (t) - 1 - i)));
            put (byte);
        }
        return *this;
    }

    template<>
    Book& Book::operator>> (      Entry &pe)
    {
        *this
            >> pe.key
            >> pe.move
            >> pe.weight
            >> pe.learn;
        return *this;
    }
    template<>
    Book& Book::operator<< (const Entry &pe)
    {
        *this
            << pe.key
            << pe.move
            << pe.weight
            << pe.learn;
        return *this;
    }

    // find_index() takes a hash-key as input, and search through the book file for the given key.
    // Returns the index of the 1st book entry with the same key as the input.
    size_t Book::find_index (      Key key)
    {
        if (!is_open ()) return streampos(-1);

        auto beg_index = size_t(0);
        auto end_index = size_t((size () - HeaderSize) / Entry::Size - 1);
        assert (beg_index <= end_index);

        Entry pe;
        while (beg_index < end_index && good ())
        {
            auto mid_index = size_t((beg_index + end_index) / 2);
            assert(mid_index >= beg_index && mid_index < end_index);

            seekg (OFFSET(mid_index), ios_base::beg);
            *this >> pe;

            if (key <= pe.key)
            {
                end_index = mid_index;
            }
            else
            {
                beg_index = mid_index + 1;
            }
        }
        assert(beg_index == end_index);

        return beg_index;
    }
    size_t Book::find_index (const Position &pos)
    {
        return find_index (pos.posi_key ());
    }
    size_t Book::find_index (const string &fen, bool c960)
    {
        return find_index (Position (fen, nullptr, c960).posi_key ());
    }

    // probe_move() tries to find a book move for the given position.
    // If no move is found returns MOVE_NONE.
    // If pick_best is true returns always the highest rated move,
    // otherwise randomly chooses one, based on the move score.
    Move Book::probe_move (const Position &pos, bool pick_best)
    {
        static PRNG pr (now ());
        if (is_open ())
        {
            Key key = pos.posi_key ();

            auto index = find_index (key);

            seekg (OFFSET(index));

            auto move = MOVE_NONE;

            Entry pe;

            u16 max_weight = 0;
            u32 weight_sum = 0;

            //vector<Entry> pes;
            //while (*this >> pe, pe.key == key && good ())
            //{
            //    pes.push_back (pe);
            //    max_weight = max (max_weight, pe.weight);
            //    weight_sum += pe.weight;
            //}
            //if (!pes.size ()) return MOVE_NONE;
            //
            //if (pick_best)
            //{
            //    auto itr = pes.begin ();
            //    while (itr != pes.end ())
            //    {
            //        pe = *itr;
            //        if (pe.weight == max_weight)
            //        {
            //            move = Move(pe.move);
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
            //    u32 rand = (pr.rand<u32> () % weight_sum);
            //    auto itr = pes.begin ();
            //    while (itr != pes.end ())
            //    {
            //        pe = *itr;
            //        if (pe.weight > rand)
            //        {
            //            move = Move(pe.move);
            //            break;
            //        }
            //        rand -= pe.weight;
            //        ++itr;
            //    }
            //}

            while (*this >> pe, pe.key == key && good ())
            {
                if (pe == MOVE_NONE) continue; // Skip MOVE_NONE

                max_weight = max (max_weight, pe.weight);
                weight_sum += pe.weight;

                if (pick_best)
                {
                    if (pe.weight == max_weight) move = Move(pe);
                }
                // Choose book move according to its score.
                // If a move has a very high score it has a higher probability
                // of being choosen than a move with a lower score.
                else
                if (weight_sum != 0)
                {
                    u16 rand = pr.rand<u16> () % weight_sum;
                    if (pe.weight > rand) move = Move(pe);
                }
                // Note that first entry is always chosen if not pick best and sum of weight = 0
                else
                if (move == MOVE_NONE)
                {
                    move = Move(pe);
                }
            }

            if (move == MOVE_NONE) return MOVE_NONE;

            // Polyglot book move is encoded as follows:
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
            // the special Move's flags (bit 14-15) that are not supported by Polyglot.
            // Polyglot use 3 bits while engine use 2 bits.
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
        }
        return MOVE_NONE;
    }

    string Book::read_entries (const Position &pos)
    {
        ostringstream oss;
        if (is_open ())
        {
            Key key = pos.posi_key ();

            auto index = find_index (key);

            seekg (OFFSET(index));

            Entry pe;
            vector<Entry> pes;
            u32 weight_sum = 0;
            while (*this >> pe, pe.key == key && good ())
            {
                if (pe == MOVE_NONE) continue; // Skip MOVE_NONE

                pes.push_back (pe);
                weight_sum += pe.weight;
            }
        
            if (pes.empty ())
            {
                std::cerr
                    << "ERROR: no such key... "
                    << std::hex << std::uppercase << key << std::nouppercase << std::dec
                    << std::endl;
            }
            else
            {
                for_each (pes.begin (), pes.end (), [&oss, &weight_sum] (Entry e)
                {
                    oss << e << " prob: " << std::setfill ('0') << std::fixed << std::width_prec (6, 2) << (weight_sum != 0 ? 100.0 * e.weight / weight_sum : 0.0) << std::setfill (' ')
                        << endl;
                });
            }
        }
        return oss.str ();
    }

}

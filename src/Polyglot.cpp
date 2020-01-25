#include "Polyglot.h"

#include <fstream>
#include <iostream>

#include "MoveGenerator.h"
#include "Notation.h"
#include "Util.h"

PolyBook Book;

using namespace std;

namespace {

    template<typename T>
    ifstream& operator>>(ifstream &ifs, T &t)
    {
        t = T();
        for (u08 i = 0; i < sizeof (t) && ifs.good(); ++i)
        {
            t = T((t << 8) + u08(ifs.get()));
        }
        return ifs;
    }
    //template<typename T>
    //ofstream& operator<<(ofstream &ofs, const T &t)
    //{
    //    for (u08 i = 0; i < sizeof (t) && ofs.good(); ++i)
    //    {
    //        ofs.put (u08(t >> (8*(sizeof (t) - 1 - i))));
    //    }
    //    return ofs;
    //}

    ifstream& operator>>(ifstream &ifs, PolyEntry &entry)
    {
        ifs >> entry.key
            >> entry.move
            >> entry.weight
            >> entry.learn;
        return ifs;
    }
    //ofstream& operator<<(ofstream &ofs, const PolyEntry &entry)
    //{
    //    ofs << entry.key
    //        << entry.move
    //        << entry.weight
    //        << entry.learn;
    //    return ofs;
    //}


    Move convert_move(const Position &pos, Move m)
    {
        // Polyglot book move is encoded as follows:
        //
        // bit 00-05: destiny square  (0...63)
        // bit 06-11: origin square   (0...63)
        // bit 12-14: promotion piece (None = 0, Knight = 1 ... Queen = 4)
        // bit    15: empty
        // Move is "0" then it should simply be ignored.
        // It seems that in that case one might as well delete the entry from the book.

        // Castling moves follow "king captures rook" representation.
        // Promotion moves have promotion piece different then our structure of move
        // So in case book move is a promotion have to convert to our representation,
        // in all the other cases can directly compare with a Move after having masked out
        // the special Move's flags (bit 14-15) that are not supported by Polyglot.
        u08 pt = (m >> 12) & PT_NO;
        if (PAWN != pt)
        {
            assert(NIHT <= pt && pt <= QUEN);
            // Set new type for promotion piece
            m = Move(/*PROMOTE +*/ ((pt - 1) << 12) + (m & 0x0FFF));
        }
        // Add special move flags and verify it is legal
        for (const auto &vm : MoveList<GenType::LEGAL>(pos))
        {
            if ((vm.move & ~PROMOTE) == m)
            {
                return vm;
            }
        }

        return MOVE_NONE;
    }

    bool move_draw(Position &pos, Move m)
    {
        StateInfo si;
        pos.do_move(m, si);
        bool dr = pos.draw(64);
        pos.undo_move(m);
        return dr;
    }

}

PolyEntry::operator string() const
{
    ostringstream oss;
    oss << " key: "    << setw(16) << setfill('0') << hex << uppercase << key << nouppercase << dec
        << " move: "   << setw( 5) << setfill(' ') << left << Move(move) << right
        << " weight: " << setw( 5) << setfill('0') << weight
        << " learn: "  << setw( 2) << setfill('0') << learn
        << setfill(' ');
    return oss.str();
}

PRNG PolyBook::prng{u64(now())};

PolyBook::PolyBook()
    : entries(nullptr)
    , entry_count(0)
    , fail_counter(0)
    , do_probe(true)
    , last_pieces(0)
    , last_piece_count(0)
    , enabled(false)
    , book_fn("")
{
}

PolyBook::~PolyBook()
{
    clear();
}

void PolyBook::clear()
{
    enabled = false;
    if (nullptr != entries)
    {
        delete[] entries;
        entries = nullptr;
    }
}

i64 PolyBook::find_index(Key key) const
{
    i64 beg = i64(0);
    i64 end = i64(entry_count);

    while (beg + 8 < end)
    {
        i64 mid = (beg + end) / 2;

        if (key > entries[mid].key)
        {
            beg = mid;
        }
        else
        if (key < entries[mid].key)
        {
            end = mid;
        }
        else // key == entries[mid].key
        {
            beg = std::max(mid - 4, i64(0));
            end = std::min(mid + 4, i64(entry_count));
        }
    }

    while (beg < end)
    {
        if (key == entries[beg].key)
        {
            while (   0 < beg
                   && key == entries[beg - 1].key)
            {
                --beg;
            }
            return beg;
        }
        ++beg;
    }

    return -1;
}
//i64 PolyBook::find_index(const Position &pos) const
//{
//    return find_index(pos.pg_key());
//}
//i64 PolyBook::find_index(const string &fen, bool c960) const
//{
//    StateInfo si;
//    return find_index(Position().setup(fen, si, nullptr, c960).pg_key());
//}

bool PolyBook::can_probe(const Position &pos)
{
    Bitboard pieces = pos.pieces();
    i32 piece_count = pos.count();

    if (   pieces != last_pieces
        //|| pop_count(pieces ^ last_pieces) > 6
        || piece_count > last_piece_count
        || piece_count < last_piece_count - 2
        || U64(0x463B96181691FC9C) == pos.pg_key())
    {
        do_probe = true;
    }

    last_pieces = pieces;
    last_piece_count = piece_count;

    return do_probe;
}

void PolyBook::initialize(const string &bk_fn)
{
    clear();

    book_fn = bk_fn;
    trim(book_fn);
    std::replace(book_fn.begin(), book_fn.end(), '\\', '/');

    if (white_spaces(book_fn))
    {
        return;
    }

    ifstream ifs(book_fn, ios_base::in|ios_base::binary);
    if (!ifs.is_open())
    {
        return;
    }

    ifs.seekg(size_t(0), ios_base::end);
    size_t filesize = ifs.tellg();
    ifs.seekg(size_t(0), ios_base::beg);

    entry_count = (filesize - HeaderSize) / sizeof (PolyEntry);
    entries = new PolyEntry[entry_count];

    if (0 != HeaderSize)
    {
        PolyEntry dummy;
        for (size_t i = 0; i < HeaderSize / sizeof (PolyEntry); ++i)
        {
            ifs >> dummy;
        }
    }
    for (size_t i = 0; i < entry_count; ++i)
    {
        ifs >> entries[i];
    }
    ifs.close();

    sync_cout << "info string Book entries found " << entry_count << " from file \'" << book_fn << "\'" << sync_endl;
    enabled = true;
}

/// PolyBook::probe() tries to find a book move for the given position.
/// If no move is found returns MOVE_NONE.
/// If pick_best is true returns always the highest rated move,
/// otherwise randomly chooses one, based on the move score.
Move PolyBook::probe(Position &pos, i16 move_num, bool pick_best)
{
    if (   !enabled
        || nullptr == entries
        || (0 != move_num && move_num < pos.move_num())
        || !can_probe(pos))
    {
        return MOVE_NONE;
    }

    auto key = pos.pg_key();

    auto index = find_index(key);
    if (0 > index)
    {
        if (4 < ++fail_counter)
        {
            // Stop probe after 4 times not in the book till position changes according to can_probe()
            do_probe = false;
            fail_counter = 0;
        }

        return MOVE_NONE;
    }

    u08 count = 0;
    u16 max_weight = 0;
    u32 sum_weight = 0;

    size_t pick1_index = index;
    size_t i = index;
    while (   i < entry_count
           && key == entries[i].key)
    {
        if (MOVE_NONE == entries[i].move) continue;

        ++count;
        max_weight = std::max(entries[i].weight, max_weight);
        sum_weight += entries[i].weight;

        // Choose the move

        if (pick_best)
        {
            if (max_weight == entries[i].weight)
            {
                pick1_index = i;
            }
        }
        else
        {
            // Move with a very high score, has a higher probability of being choosen.
            if (   0 != sum_weight
                && (prng.rand<u32>() % sum_weight) < entries[i].weight)
            {
                pick1_index = i;
            }
        }
        ++i;
    }

    Move move;
    move = Move(entries[pick1_index].move);
    if (MOVE_NONE == move)
    {
        return MOVE_NONE;
    }

    move = convert_move(pos, move);

    if (   !pos.draw(64)
        || 1 >= count)
    {
        return move;
    }

    if (!move_draw(pos, move))
    {
        return move;
    }

    // Special case draw position and more than one moves available

    size_t pick2_index = index;
    if (pick2_index == pick1_index)
    {
        ++pick2_index;
        assert(pick2_index < i);
    }

    move = Move(entries[pick2_index].move);
    if (MOVE_NONE == move)
    {
        return MOVE_NONE;
    }

    move = convert_move(pos, move);

    if (!move_draw(pos, move))
    {
        return move;
    }

    return MOVE_NONE;
}

string PolyBook::show(const Position &pos) const
{
    if (   nullptr == entries
        || !enabled)
    {
        return "Book entries empty.";
    }

    auto key = pos.pg_key();

    auto index = find_index(key);
    if (0 > index)
    {
        return "Book entries not found.";
    }

    ostringstream oss;
    list<PolyEntry> list_entries;
    u32 sum_weight = 0;
    while (   size_t(index) < entry_count
           && key == entries[index].key)
    {
        list_entries.push_back(entries[index]);
        sum_weight += entries[index].weight;
        ++index;
    }
    if (!list_entries.empty())
    {
        list_entries.sort();
        list_entries.reverse();
        oss << "\nBook entries: " << list_entries.size();
        for (auto entry : list_entries)
        {
            entry.move = convert_move(pos, Move(entry.move));
            oss << "\n"
                << entry
                << " prob: "
                << setw(7)
                << setfill('0')
                << fixed << setprecision(4) << (0 != sum_weight ? 100.0 * entry.weight / sum_weight : 0.0)
                << setfill(' ');
        }
    }
    return oss.str();
}

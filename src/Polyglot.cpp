#include "Polyglot.h"

#include <fstream>
#include <iostream>
#include <list>

#include "Helper.h"
#include "MoveGenerator.h"
#include "Notation.h"

PolyBook Book;

using namespace std;

namespace {

    template<typename T>
    ifstream& operator>>(ifstream &ifs, T &t) {
        t = T();
        for (u08 idx = 0; idx < sizeof (T) && ifs.good(); ++idx) {
            t = T((t << 8) + u08(ifs.get()));
        }
        return ifs;
    }
    // template<typename T>
    // ofstream& operator<<(ofstream &ofs, const T &t) {
    //    for (u08 idx = 0; idx < sizeof (T) && ofs.good(); ++idx) {
    //        ofs.put(u08(t >> (8*(sizeof (T) - 1 - idx))));
    //    }
    //    return ofs;
    // }

    ifstream& operator>>(ifstream &ifs, PolyEntry &pe) {
        ifs >> pe.key
            >> pe.move
            >> pe.weight
            >> pe.learn;
        return ifs;
    }
    // ofstream& operator<<(ofstream &ofs, const PolyEntry &pe) {
    //    ofs << pe.key
    //        << pe.move
    //        << pe.weight
    //        << pe.learn;
    //    return ofs;
    // }

    // Converts polyglot move to engine move
    Move polyMove(Move m, const Position &pos) {
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
        u08 pt = (m >> 12) & PIECE_TYPES;
        if (PAWN < pt) {
            assert(NIHT <= pt && pt <= QUEN);
            // Set new type for promotion piece
            m = Move(/*PROMOTE +*/ ((pt - 1) << 12) + mIndex(m));
        }
        // Add special move flags and verify it is legal
        for (const auto &vm : MoveList<GenType::LEGAL>(pos)) {
            if ((vm.move & ~PROMOTE) == m) {
                return vm;
            }
        }

        return MOVE_NONE;
    }

    bool moveIsDraw(Position &pos, Move m) {
        StateInfo si;
        pos.doMove(m, si);
        bool dr = pos.draw(64);
        pos.undoMove(m);
        return dr;
    }

}

PolyEntry::PolyEntry(u64 k, u16 m, u16 w, u32 l)
    : key{k}
    , move{m}
    , weight{w}
    , learn{l}
{}

bool PolyEntry::operator==(const PolyEntry &pe) const {
    return key == pe.key
        && move == pe.move
        && weight == pe.weight;
}
bool PolyEntry::operator!=(const PolyEntry &pe) const {
    return key != pe.key
        || move != pe.move
        || weight != pe.weight;
}

bool PolyEntry::operator>(const PolyEntry &pe) const {
    return key != pe.key ?
                key > pe.key :
                weight != pe.weight ?
                    weight > pe.weight :
                    move > pe.move;
}
bool PolyEntry::operator<(const PolyEntry &pe) const {
    return key != pe.key ?
                key < pe.key :
                weight != pe.weight ?
                    weight < pe.weight :
                    move < pe.move;
}

bool PolyEntry::operator>=(const PolyEntry &pe) const {
    return key != pe.key ?
                key >= pe.key :
                weight != pe.weight ?
                    weight >= pe.weight :
                    move >= pe.move;
}
bool PolyEntry::operator<=(const PolyEntry &pe) const {
    return key != pe.key ?
                key <= pe.key :
                weight != pe.weight ?
                    weight <= pe.weight :
                    move <= pe.move;
}

bool PolyEntry::operator==(Move m) const { return move == m; }
bool PolyEntry::operator!=(Move m) const { return move != m; }

string PolyEntry::toString() const {
    ostringstream oss;
    oss << std::right
        << " key: "     << std::setw(16) << std::setfill('0') << std::hex << std::uppercase << key << std::nouppercase << std::dec
        << std::left
        << " move: "    << std::setw( 5) << std::setfill(' ') << Move(move)
        << std::right
        << " weight: "  << std::setw( 5) << std::setfill('0') << weight
        << " learn: "   << std::setw( 2) << std::setfill('0') << learn;
    return oss.str();
}

ostream& operator<<(ostream &os, const PolyEntry &pe) {
    os << pe.toString();
    return os;
}

/// ----------------

PolyBook::PolyBook()
    : entries{ nullptr }
    , entryCount{ 0 }
    , doProbe{ true }
    , prevPieces{ 0 }
    , prevPieceCount{ 0 }
    , failCount{ 0 }
    , enabled{ false }
    , bookFn{}
{}

PolyBook::~PolyBook() {
    clear();
}

void PolyBook::clear() {
    enabled = false;
    if (nullptr != entries)
    {
        delete[] entries;
        entries = nullptr;
    }
}

i64 PolyBook::findIndex(Key key) const {
    i64 beg{ 0 };
    i64 end{ i64(entryCount) };

    while (beg + 8 < end) {
        i64 mid{ (beg + end) / 2 };

        if (key > entries[mid].key) {
            beg = mid;
        }
        else
        if (key < entries[mid].key) {
            end = mid;
        }
        else { // key == entries[mid].key
            beg = std::max(mid - 4, i64(0));
            end = std::min(mid + 4, i64(entryCount));
        }
    }

    while (beg < end) {
        if (key == entries[beg].key) {
            while (0 < beg
                && key == entries[beg - 1].key) {
                --beg;
            }
            return beg;
        }
        ++beg;
    }

    return -1;
}
//i64 PolyBook::findIndex(const Position &pos) const {
//    return findIndex(pos.pgKey());
//}
//i64 PolyBook::findIndex(const string &fen) const {
//    StateInfo si;
//    return findIndex(Position().setup(fen, si, nullptr).pgKey());
//}

bool PolyBook::canProbe(const Position &pos) {
    Bitboard pieces{ pos.pieces() };
    i32 pieceCount{ pos.count() };

    if (pieces != prevPieces
     || popCount(pieces ^ prevPieces) > 6
     || pieceCount > prevPieceCount
     || pieceCount < prevPieceCount - 2
     || U64(0x463B96181691FC9C) == pos.pgKey()) {
        doProbe = true;
    }

    prevPieces = pieces;
    prevPieceCount = pieceCount;

    return doProbe;
}

void PolyBook::initialize(const string &bkFn) {
    clear();

    bookFn = bkFn;
    trim(bookFn);
    std::replace(bookFn.begin(), bookFn.end(), '\\', '/');

    if (whiteSpaces(bookFn)) {
        return;
    }

    ifstream ifs{ bookFn, ios::in|ios::binary };
    if (!ifs.is_open()) {
        return;
    }

    ifs.seekg(size_t(0), ios::end);
    size_t filesize = ifs.tellg();
    ifs.seekg(size_t(0), ios::beg);

    entryCount = (filesize - HeaderSize) / sizeof (PolyEntry);
    entries = new PolyEntry[entryCount];

    if (0 != HeaderSize) {
        PolyEntry dummy;
        for (size_t idx = 0; idx < HeaderSize / sizeof (PolyEntry); ++idx) {
            ifs >> dummy;
        }
    }
    for (size_t idx = 0; idx < entryCount; ++idx) {
        ifs >> entries[idx];
    }
    ifs.close();

    cout << "info string Book entries found " << entryCount << " from file \'" << bookFn << "\'" << endl;
    enabled = true;
}

/// PolyBook::probe() tries to find a book move for the given position.
/// If no move is found returns MOVE_NONE.
/// If pickBest is true returns always the highest rated move,
/// otherwise randomly chooses one, based on the move score.
Move PolyBook::probe(Position &pos, i16 moveCount, bool pickBest)
{
    static PRNG prng{ u64(now()) };

    if (!enabled
     || nullptr == entries
     || (0 != moveCount && moveCount < pos.moveCount())
     || !canProbe(pos)) {
        return MOVE_NONE;
    }

    auto key{ pos.pgKey() };
    auto index{ findIndex(key) };
    if (0 > index) {
        if (4 < ++failCount) {
            // Stop probe after 4 times not in the book till position changes according to canProbe()
            doProbe = false;
            failCount = 0;
        }

        return MOVE_NONE;
    }

    u08 count{0};
    u16 maxWeight{0};
    u32 sumWeight{0};

    size_t pick1Index = index;
    size_t idx = index;
    while (idx < entryCount
        && key == entries[idx].key) {
        if (MOVE_NONE == entries[idx].move) continue;

        ++count;
        maxWeight = std::max(entries[idx].weight, maxWeight);
        sumWeight += entries[idx].weight;

        // Choose the move
        if (pickBest) {
            if (maxWeight == entries[idx].weight) {
                pick1Index = idx;
            }
        }
        else {
            // Move with a very high score, has a higher probability of being choosen.
            if (0 != sumWeight
             && (prng.rand<u32>() % sumWeight) < entries[idx].weight) {
                pick1Index = idx;
            }
        }
        ++idx;
    }

    Move move;

    move = Move(entries[pick1Index].move);
    if (MOVE_NONE == move) {
        return MOVE_NONE;
    }

    move = polyMove(move, pos);

    if (!pos.draw(64)
     || 1 >= count) {
        return move;
    }

    if (!moveIsDraw(pos, move)) {
        return move;
    }

    // Special case draw position and more than one moves available

    size_t pick2Index = index;
    if (pick2Index == pick1Index) {
        ++pick2Index;
        assert(pick2Index < idx);
    }

    move = Move(entries[pick2Index].move);
    if (MOVE_NONE == move) {
        return MOVE_NONE;
    }

    move = polyMove(move, pos);

    if (!moveIsDraw(pos, move)) {
        return move;
    }

    return MOVE_NONE;
}

string PolyBook::show(const Position &pos) const {
    if (nullptr == entries
     || !enabled) {
        return "Book entries empty.";
    }

    auto key{ pos.pgKey() };
    auto index{ findIndex(key) };
    if (0 > index) {
        return "Book entries not found.";
    }

    ostringstream oss;

    list<PolyEntry> peList;
    u32 sumWeight{0};
    while (size_t(index) < entryCount
        && key == entries[index].key) {
        peList.push_back(entries[index]);
        sumWeight += entries[index].weight;
        ++index;
    }

    if (!peList.empty()) {
        peList.sort();
        peList.reverse();
        oss << "\nBook entries: " << peList.size() << endl;
        for (auto &pe : peList) {
            pe.move = polyMove(Move(pe.move), pos);
            oss << pe
                << " prob: "
                << std::setfill('0')
                << std::setw(7) << std::fixed << std::setprecision(4) << (0 != sumWeight ? 100.0 * pe.weight / sumWeight : 0.0)
                << std::setfill(' ');
        }
    }
    return oss.str();
}

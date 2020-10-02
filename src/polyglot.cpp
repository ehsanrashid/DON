#include "polyglot.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "movegenerator.h"
#include "notation.h"
#include "helper/string_view.h"

PolyBook Book;

namespace {

    template<typename T>
    std::ifstream& operator>>(std::ifstream &ifstream, T &t) {
        t = T();
        for (u08 idx = 0; idx < sizeof (T) && ifstream.good(); ++idx) {
            t = T((t << 8) + u08(ifstream.get()));
        }
        return ifstream;
    }
    //template<typename T>
    //std::ofstream& operator<<(std::ofstream &ofstream, T const &t) {
    //    for (u08 idx = 0; idx < sizeof (T) && ofstream.good(); ++idx) {
    //        ofstream.put(u08(t >> (8 * (sizeof (T) - 1 - idx))));
    //    }
    //    return ofstream;
    //}

    std::ifstream& operator>>(std::ifstream &ifstream, PolyEntry &pe) {
        ifstream >> pe.key
                 >> pe.move
                 >> pe.weight
                 >> pe.learn;
        return ifstream;
    }
    //std::ofstream& operator<<(std::ofstream &ofstream, PolyEntry const &pe) {
    //    ofstream << pe.key
    //             << pe.move
    //             << pe.weight
    //             << pe.learn;
    //    return ofstream;
    //}

    // Converts polyglot move to engine move
    Move polyMove(Move m, Position const &pos) {
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
        u08 const pt( (m >> 12) & 7 );
        if (pt != 0) {
            // Set new type for promotion piece
            m = Move(/*PROMOTE +*/ ((pt - 1) << 12) + mMask(m));
        }
        // Add special move flags and verify it is legal
        for (auto const &vm : MoveList<LEGAL>(pos)) {
            if ((vm.move & ~PROMOTE) == m) {
                return vm;
            }
        }
        return MOVE_NONE;
    }

    bool moveIsDraw(Position &pos, Move m) {
        StateInfo si;
        pos.doMove(m, si);
        bool const dr{ pos.draw(64) };
        pos.undoMove(m);
        return dr;
    }

}

bool PolyEntry::operator==(PolyEntry const &pe) const noexcept {
    return key == pe.key
        && move == pe.move
        && weight == pe.weight;
}
bool PolyEntry::operator!=(PolyEntry const &pe) const noexcept {
    return key != pe.key
        || move != pe.move
        || weight != pe.weight;
}

bool PolyEntry::operator>(PolyEntry const &pe) const noexcept {
    return key != pe.key       ? key > pe.key :
           weight != pe.weight ? weight > pe.weight :
                                 move > pe.move;
}
bool PolyEntry::operator<(PolyEntry const &pe) const noexcept {
    return key != pe.key       ? key < pe.key :
           weight != pe.weight ? weight < pe.weight :
                                 move < pe.move;
}

bool PolyEntry::operator>=(PolyEntry const &pe) const noexcept {
    return key != pe.key       ? key >= pe.key :
           weight != pe.weight ? weight >= pe.weight :
                                 move >= pe.move;
}
bool PolyEntry::operator<=(PolyEntry const &pe) const noexcept {
    return key != pe.key       ? key <= pe.key :
           weight != pe.weight ? weight <= pe.weight :
                                 move <= pe.move;
}

bool PolyEntry::operator==(Move m) const noexcept { return move == m; }
bool PolyEntry::operator!=(Move m) const noexcept { return move != m; }

std::string PolyEntry::toString() const {
    std::ostringstream oss{};
    oss << std::right
        << " key: " << std::setw(16) << std::setfill('0') << std::hex << std::uppercase << key << std::nouppercase << std::dec
        << std::left
        << " move: " << std::setw(5) << std::setfill(' ') << Move(move)
        << std::right
        << " weight: " << std::setw(5) << std::setfill('0') << weight
        << " learn: " << std::setw(2) << std::setfill('0') << learn;
    return oss.str();
}

std::ostream& operator<<(std::ostream &ostream, PolyEntry const &pe) {
    ostream << pe.toString();
    return ostream;
}

/// ----------------

PolyBook::~PolyBook() {
    clear();
}

void PolyBook::clear() noexcept {

    enabled = false;
    if (entry != nullptr) {
        delete[] entry;
        entry = nullptr;
    }
}

i64 PolyBook::findIndex(Key pgKey) const noexcept {
    i64 beg{ 0 };
    i64 end{ i64(entryCount) };

    while (beg + 8 < end) {
        i64 mid{ (beg + end) / 2 };

        if (pgKey > entry[mid].key) {
            beg = mid;
        }
        else
        if (pgKey < entry[mid].key) {
            end = mid;
        }
        else { // pgKey == entry[mid].key
            beg = std::max(mid - 4, i64(0));
            end = std::min(mid + 4, i64(entryCount));
        }
    }

    while (beg < end) {
        if (pgKey == entry[beg].key) {
            while (beg > 0
                && pgKey == entry[beg - 1].key) {
                --beg;
            }
            return beg;
        }
        ++beg;
    }

    return -1;
}
//i64 PolyBook::findIndex(Position const &pos) const noexcept {
//    return findIndex(pos.pgKey());
//}
//i64 PolyBook::findIndex(std::string_view fen) const noexcept {
//    StateInfo si;
//    return findIndex(Position().setup(fen, si, nullptr).pgKey());
//}

bool PolyBook::canProbe(Position const &pos) noexcept {

    if (pieces != pos.pieces()
     || popCount(pieces ^ pos.pieces()) > 6
     || pieceCount < pos.count()
     || pieceCount > pos.count() + 2
     || pos.pgKey() == U64(0x463B96181691FC9C)) {
        doProbe = true;
    }

    pieces = pos.pieces();
    pieceCount = pos.count();

    return doProbe;
}

void PolyBook::initialize(std::string_view bookFile) {

    clear();

    filename = bookFile;
    std::replace(filename.begin(), filename.end(), '\\', '/');
    filename = trim(filename);
    if (filename.empty()) {
        return;
    }

    std::ifstream ifstream{ filename, std::ios::in|std::ios::binary };
    if (!ifstream.is_open()) {
        return;
    }

    ifstream.seekg(0, std::ios::end);
    u64 const fileSize = ifstream.tellg();
    ifstream.seekg(0, std::ios::beg);

    entryCount = (fileSize - HeaderSize) / sizeof (PolyEntry);
    entry = new PolyEntry[entryCount];
    if (entry == nullptr) {
        return;
    }
    enabled = true;

    if (HeaderSize != 0) {
        PolyEntry dummy;
        for (u64 idx = 0; idx < HeaderSize / sizeof (PolyEntry); ++idx) {
            ifstream >> dummy;
        }
    }
    for (u64 idx = 0; idx < entryCount; ++idx) {
        ifstream >> entry[idx];
    }
    ifstream.close();

    std::cout << "info string Book entries found " << entryCount << " from file \'" << filename << "\'" << std::endl;
}

/// PolyBook::probe() tries to find a book move for the given position.
/// If no move is found returns MOVE_NONE.
/// If pickBest is true returns always the highest rated move,
/// otherwise randomly chooses one, based on the move score.
Move PolyBook::probe(Position &pos, i16 moveCount, bool pickBest) {

    static PRNG prng(now());

    if (!enabled
     || entry == nullptr
     || (moveCount != 0
      && moveCount < pos.moveCount())
     || !canProbe(pos)) {
        return MOVE_NONE;
    }

    auto const pgKey{ pos.pgKey() };
    auto const index{ findIndex(pgKey) };
    if (index < 0) {
        if (++failCount > 4) {
            // Stop probe after 4 times not in the book till position changes according to canProbe()
            doProbe = false;
            failCount = 0;
        }
        return MOVE_NONE;
    }

    u08 count{ 0 };
    u16 maxWeight{ 0 };
    u32 sumWeight{ 0 };

    u64 pick1Index = index;
    u64 idx = index;
    while (idx < entryCount
        && pgKey == entry[idx].key) {

        if (entry[idx].move == MOVE_NONE) {
            continue;
        }
        ++count;
        maxWeight = std::max(entry[idx].weight, maxWeight);
        sumWeight += entry[idx].weight;

        // Choose the move
        if (pickBest) {
            if (maxWeight == entry[idx].weight) {
                pick1Index = idx;
            }
        }
        else {
            // Move with a very high score, has a higher probability of being choosen.
            if (sumWeight != 0
             && (prng.rand<u32>() % sumWeight) < entry[idx].weight) {
                pick1Index = idx;
            }
        }
        ++idx;
    }

    Move move;

    move = Move(entry[pick1Index].move);
    if (move == MOVE_NONE) {
        return MOVE_NONE;
    }

    move = polyMove(move, pos);

    if (!pos.draw(64)
     || count <= 1) {
        return move;
    }

    if (!moveIsDraw(pos, move)) {
        return move;
    }

    // Special case draw position and more than one moves available

    u64 pick2Index = index;
    if (pick2Index == pick1Index) {
        ++pick2Index;
        assert(pick2Index < idx);
    }

    move = Move(entry[pick2Index].move);
    if (move == MOVE_NONE) {
        return MOVE_NONE;
    }

    move = polyMove(move, pos);

    if (!moveIsDraw(pos, move)) {
        return move;
    }

    return MOVE_NONE;
}

std::string PolyBook::show(Position const &pos) const {
    if (entry == nullptr
     || !enabled) {
        return "Book entries empty.";
    }

    auto const key{ pos.pgKey() };
    auto index{ findIndex(key) };
    if (index < 0) {
        return "Book entries not found.";
    }

    std::vector<PolyEntry> peSet;
    u32 sumWeight{ 0 };
    while (u64(index) < entryCount
        && key == entry[index].key) {
        peSet.push_back(entry[index]);
        sumWeight += entry[index].weight;
        ++index;
    }

    if (peSet.empty()) {
        return "No Book entry found";
    }

    std::sort(peSet.begin(), peSet.end());
    std::reverse(peSet.begin(), peSet.end());

    std::ostringstream oss{};
    oss << "\nBook entries: " << peSet.size() << '\n';
    for (auto &pe : peSet) {
        pe.move = polyMove(Move(pe.move), pos);
        auto const prob{ sumWeight != 0 ? 100.0 * pe.weight / sumWeight : 0.0 };
        oss << pe
            << " prob: "
            << std::setfill('0')
            << std::setw(7) << std::fixed << std::setprecision(4) << prob
            << std::setfill(' ');
    }
    return oss.str();

}

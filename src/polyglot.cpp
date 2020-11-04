#include "polyglot.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "movegenerator.h"
#include "notation.h"
#include "helper/prng.h"
#include "helper/string_view.h"

PolyBook Book;

namespace {

    template<typename T>
    std::ifstream& operator>>(std::ifstream &ifstream, T &t) {
        t = T();
        for (uint8_t idx = 0; idx < sizeof(T) && ifstream.good(); ++idx) {
            t = T((t << 8) + uint8_t(ifstream.get()));
        }
        return ifstream;
    }
    //template<typename T>
    //std::ofstream& operator<<(std::ofstream &ofstream, T const &t) {
    //    for (uint8_t idx = 0; idx < sizeof(T) && ofstream.good(); ++idx) {
    //        ofstream.put(uint8_t(t >> (8 * (sizeof(T) - 1 - idx))));
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
        uint8_t const pt( (m >> 12) & 7 );
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
    return !(*this == pe);
}

bool PolyEntry::operator>(PolyEntry const &pe) const noexcept {
    return key    != pe.key    ? key    > pe.key :
           weight != pe.weight ? weight > pe.weight :
                                 move   > pe.move;
}
bool PolyEntry::operator<(PolyEntry const &pe) const noexcept {
    return key    != pe.key    ? key    < pe.key :
           weight != pe.weight ? weight < pe.weight :
                                 move   < pe.move;
}

bool PolyEntry::operator>=(PolyEntry const &pe) const noexcept {
    return !(*this < pe);
}
bool PolyEntry::operator<=(PolyEntry const &pe) const noexcept {
    return !(*this > pe);
}

bool PolyEntry::operator==(Move m) const noexcept { return move == m; }
bool PolyEntry::operator!=(Move m) const noexcept { return move != m; }

std::string PolyEntry::toString() const {
    std::ostringstream oss;
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

constexpr PolyBook::PolyBook() noexcept :
    enabled{ false },
    entryTable{ nullptr },
    entryCount{ 0 },
    pieces{ 0 },
    failCount{ 0 } {
}

PolyBook::~PolyBook() noexcept {
    clear();
}

void PolyBook::clear() noexcept {

    enabled = false;
    if (entryTable != nullptr) {
        delete[] entryTable;
        entryTable = nullptr;
    }
}

int64_t PolyBook::findIndex(Key pgKey) const noexcept {
    int64_t beg{ 0 };
    int64_t end{ int64_t(entryCount) };

    while (beg + 8 < end) {
        int64_t mid{ (beg + end) / 2 };

        if (pgKey > entryTable[mid].key) {
            beg = mid;
        } else
        if (pgKey < entryTable[mid].key) {
            end = mid;
        } else { // pgKey == entry[mid].key
            beg = std::max(mid - 4, int64_t(0));
            end = std::min(mid + 4, int64_t(entryCount));
        }
    }

    while (beg < end) {
        if (pgKey == entryTable[beg].key) {
            while (beg > 0
                && pgKey == entryTable[beg - 1].key) {
                --beg;
            }
            return beg;
        }
        ++beg;
    }

    return -1;
}
//int64_t PolyBook::findIndex(Position const &pos) const noexcept {
//    return findIndex(pos.pgKey());
//}
//int64_t PolyBook::findIndex(std::string_view fen) const noexcept {
//    StateInfo si;
//    return findIndex(Position().setup(fen, si, nullptr).pgKey());
//}

bool PolyBook::canProbe(Position const &pos) noexcept {

    if (pieces != pos.pieces()
     || popCount(pieces ^ pos.pieces()) > 6
     || popCount(pieces) > popCount(pos.pieces()) + 2
     || pos.pgKey() == U64(0x463B96181691FC9C)) {
        failCount = 0;
    }

    pieces = pos.pieces();
    // Stop probe after 4 times not in the book till position changes according to canProbe()
    return failCount <= 4;
}

void PolyBook::initialize(std::string_view bookFile) {

    clear();

    std::string filename{ bookFile };
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
    uint64_t const fileSize = ifstream.tellg();
    ifstream.seekg(0, std::ios::beg);

    entryCount = (fileSize - HeaderSize) / sizeof(PolyEntry);
    entryTable = new PolyEntry[entryCount];
    if (entryTable == nullptr) {
        return;
    }
    enabled = true;

    if (HeaderSize != 0) {
        PolyEntry dummy;
        for (uint64_t idx = 0; idx < HeaderSize / sizeof(PolyEntry); ++idx) {
            ifstream >> dummy;
        }
    }
    for (uint64_t idx = 0; idx < entryCount; ++idx) {
        ifstream >> entryTable[idx];
    }
    ifstream.close();

    std::cout << "info string Book entries found " << entryCount << " from file \'" << filename << "\'" << std::endl;
}

/// PolyBook::probe() tries to find a book move for the given position.
/// If no move is found returns MOVE_NONE.
/// If pickBest is true returns always the highest rated move,
/// otherwise randomly chooses one, based on the move score.
Move PolyBook::probe(Position &pos, int16_t moveCount, bool pickBest) {

    static PRNG prng(now());

    if (!enabled
     || entryTable == nullptr
     || (moveCount != 0
      && moveCount < pos.moveCount())
     || !canProbe(pos)) {
        return MOVE_NONE;
    }

    auto const pgKey{ pos.pgKey() };
    auto const index{ findIndex(pgKey) };
    if (index < 0) {
        ++failCount;
        return MOVE_NONE;
    }

    uint8_t count{ 0 };
    uint16_t maxWeight{ 0 };
    uint32_t sumWeight{ 0 };

    uint64_t pick1Index = index;
    uint64_t idx = index;
    while (idx < entryCount
        && pgKey == entryTable[idx].key) {

        if (entryTable[idx].move == MOVE_NONE) {
            continue;
        }
        ++count;
        maxWeight = std::max(entryTable[idx].weight, maxWeight);
        sumWeight += entryTable[idx].weight;

        // Choose the move
        if (pickBest) {
            if (maxWeight == entryTable[idx].weight) {
                pick1Index = idx;
            }
        } else {
            // Move with a very high score, has a higher probability of being choosen.
            if (sumWeight != 0
             && (prng.rand<uint32_t>() % sumWeight) < entryTable[idx].weight) {
                pick1Index = idx;
            }
        }
        ++idx;
    }

    Move move;

    move = Move(entryTable[pick1Index].move);
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

    uint64_t pick2Index = index;
    if (pick2Index == pick1Index) {
        ++pick2Index;
        assert(pick2Index < idx);
    }

    move = Move(entryTable[pick2Index].move);
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
    if (entryTable == nullptr
     || !enabled) {
        return "Book entries empty.";
    }

    auto const key{ pos.pgKey() };
    auto index{ findIndex(key) };
    if (index < 0) {
        return "Book entries not found.";
    }

    std::vector<PolyEntry> peSet;
    uint32_t sumWeight{ 0 };
    while (uint64_t(index) < entryCount
        && key == entryTable[index].key) {
        peSet.push_back(entryTable[index]);
        sumWeight += entryTable[index].weight;
        ++index;
    }

    if (peSet.empty()) {
        return "No Book entry found";
    }

    std::sort(peSet.begin(), peSet.end());
    std::reverse(peSet.begin(), peSet.end());

    std::ostringstream oss;
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

#include "transposition.h"

#include <cstdlib>
#include <cstring> // For memset()
#include <algorithm>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "movegenerator.h"
#include "thread.h"
#include "uci.h"
#include "helper/string_view.h"
#include "helper/memoryhandler.h"

TTable TT;

uint8_t TEntry::Generation{ 0 };

/// TCluster::probe()
/// If the position is found, it returns true and a pointer to the found entry.
/// Otherwise, it returns false and a pointer to an empty or least valuable entry to be replaced later.
TEntry* TCluster::probe(const uint16_t key16, bool &hit) noexcept {
    // Find an entry to be replaced according to the replacement strategy.
    auto *ite{ entry };
    auto *rte{ ite }; // Default first
    auto const *ete{ entry + EntryPerCluster };
    for (; ite != ete; ++ite) {
        if (ite->k16 == key16
         || ite->d08 == 0) {
            // Refresh entry
            ite->refresh();
            return hit = ite->d08 != 0, ite;
        }
        // Replacement strategy.
        // Due to packed storage format for generation and its cyclic nature
        // add 263 (256 + 7 [4 + BOUND_EXACT] to keep the unrelated lowest three bits from affecting the result)
        // to calculate the entry age correctly even after generation overflows into the next cycle.
        if (rte->worth() > ite->worth()) {
            rte = ite;
        }
    }
    return hit = false, rte;
}


constexpr TTable::TTable() noexcept :
    clusterTable{ nullptr },
    clusterCount{ 0 } {
}

TTable::~TTable() noexcept {
    free();
}

/// size() returns hash size in MB
uint32_t TTable::size() const noexcept {
    return uint32_t((clusterCount * sizeof(TCluster)) >> 20);
}

/// TTable::resize() sets the size of the transposition table, measured in MB.
/// Transposition table consists of a power of 2 number of clusters and
/// each cluster consists of EntryPerCluster number of TTEntry.
bool TTable::resize(size_t memSize) {

    free();

    clusterCount = (memSize << 20) / sizeof(TCluster);
    assert(clusterCount % 2 == 0);
    clusterTable = static_cast<TCluster*>(allocAlignedLargePages(clusterCount * sizeof(TCluster)));
    if (clusterTable == nullptr) {
        clusterCount = 0;
        std::cerr << "ERROR: Hash memory allocation failed for TT " << memSize << " MB" << '\n';
        return false;
    }

    clear();
    //sync_cout << "info string Hash memory " << memSize << " MB" << sync_endl;
    return true;
}

/// TTable::autoResize() set size automatically
void TTable::autoResize(size_t memSize) {
    Threadpool.stopThinking();

    auto mSize{ std::clamp(memSize, MinHashSize, MaxHashSize) };
    while (mSize >= MinHashSize) {
        if (resize(mSize)) {
            return;
        }
        mSize >>= 1;
    }
    std::exit(EXIT_FAILURE);
}
/// TTable::clear() clear the entire transposition table in a multi-threaded way.
void TTable::clear() {
    assert(clusterTable != nullptr
        && clusterCount != 0);

    if (Options["Retain Hash"]) {
        return;
    }

    std::vector<std::thread> threads;
    auto const threadCount{ optionThreads() };
    for (uint16_t index = 0; index < threadCount; ++index) {
        threads.emplace_back(
            [this, threadCount, index]() {

                if (threadCount > 8) {
                    WinProcGroup::bind(index);
                }
                // Each thread will zero its part of the hash table
                auto const stride{ clusterCount / threadCount };
                auto const start{ stride * index };
                auto const count{ index != threadCount - 1 ? stride : clusterCount - start };
                std::memset(&clusterTable[start], 0, count * sizeof(TCluster));
            });
    }

    for (auto &th : threads) {
        th.join();
    }

    threads.clear();
    //sync_cout << "info string Hash cleared" << sync_endl;
}

void TTable::free() noexcept {
    freeAlignedLargePages(clusterTable);
    clusterTable = nullptr;
    clusterCount = 0;
}

/// TTable::hashFull() returns an approximation of the per-mille of the
/// all transposition entries during a search which have received
/// at least one write during the current search.
/// It is used to display the "info hashfull ..." information in UCI.
/// "the hash is <x> per mill full", the engine should send this info regularly.
/// hash, are using <x>%. of the state of full.
uint32_t TTable::hashFull() const noexcept {
    uint32_t entryCount{ 0 };
    auto const *etc{ clusterTable + std::min((uint64_t)clusterCount, 1000ULL) };
    for (auto *itc{ clusterTable }; itc != etc; ++itc) {
        entryCount += itc->freshEntryCount();
    }
    return entryCount / TCluster::EntryPerCluster;
}

/// TTable::extractNextMove() extracts next move after this move.
Move TTable::extractNextMove(Position &pos, Move m) const noexcept {
    assert(m != MOVE_NONE
        && MoveList<LEGAL>(pos).contains(m));

    StateInfo si;
    ASSERT_ALIGNED(&si, CacheLineSize);
    pos.doMove(m, si);
    bool ttHit;
    auto const *const tte{ probe(pos.posiKey(), ttHit) };
    auto nm{ ttHit ? tte->move() : MOVE_NONE };
    if (nm != MOVE_NONE
     && !(pos.pseudoLegal(nm)
       && pos.legal(nm))) {
        nm = MOVE_NONE;
    }
    assert(nm == MOVE_NONE
        || MoveList<LEGAL>(pos).contains(nm));
    pos.undoMove(m);

    return nm;
}

/// TTable::save() saves hash to file
void TTable::save(std::string_view hashFile) const {
    if (whiteSpaces(hashFile)) {
        return;
    }
    std::ofstream ofstream{ hashFile.data(), std::ios::out|std::ios::binary };
    if (!ofstream.is_open()) {
        return;
    }
    ofstream << *this;
    ofstream.close();
    sync_cout << "info string Hash saved to file \'" << hashFile << "\'" << sync_endl;
}
/// TTable::load() loads hash from file
void TTable::load(std::string_view hashFile) {
    if (whiteSpaces(hashFile)) {
        return;
    }
    std::ifstream ifstream{ hashFile.data(), std::ios::in|std::ios::binary };
    if (!ifstream.is_open()) {
        return;
    }
    ifstream >> *this;
    ifstream.close();
    sync_cout << "info string Hash loaded from file \'" << hashFile << "\'" << sync_endl;
}

namespace {

    constexpr uint32_t BufferSize{ 0x1000 };
}

std::ostream& operator<<(std::ostream &ostream, TTable const &tt) {
    uint32_t const memSize{ tt.size() };
    uint8_t const dummy{ 0 };
    ostream.write(reinterpret_cast<char const*>(&memSize), sizeof(memSize));
    ostream.write(reinterpret_cast<char const*>(&dummy), sizeof(dummy));
    ostream.write(reinterpret_cast<char const*>(&dummy), sizeof(dummy));
    ostream.write(reinterpret_cast<char const*>(&dummy), sizeof(dummy));
    ostream.write(reinterpret_cast<char const*>(&TEntry::Generation), sizeof(TEntry::Generation));
    for (size_t i = 0; i < tt.clusterCount / BufferSize; ++i) {
        ostream.write(reinterpret_cast<char const*>(&tt.clusterTable[i*BufferSize]), sizeof(TCluster)*BufferSize);
    }
    return ostream;
}

std::istream& operator>>(std::istream &istream, TTable       &tt) {
    uint32_t memSize;
    uint8_t dummy;
    istream.read(reinterpret_cast<char*>(&memSize), sizeof(memSize));
    istream.read(reinterpret_cast<char*>(&dummy), sizeof(dummy));
    istream.read(reinterpret_cast<char*>(&dummy), sizeof(dummy));
    istream.read(reinterpret_cast<char*>(&dummy), sizeof(dummy));
    istream.read(reinterpret_cast<char*>(&TEntry::Generation), sizeof(TEntry::Generation));
    tt.resize(memSize);
    for (size_t i = 0; i < tt.clusterCount / BufferSize; ++i) {
        istream.read(reinterpret_cast<char*>(&tt.clusterTable[i*BufferSize]), sizeof(TCluster)*BufferSize);
    }
    return istream;
}

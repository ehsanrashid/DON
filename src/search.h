/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "misc.h"
#include "movepick.h"
#include "numa.h"
#include "polybook.h"
#include "position.h"
#include "score.h"
#include "timeman.h"
#include "types.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include "syzygy/tbprobe.h"

namespace DON {

class OptionsMap;
class ThreadPool;
class TranspositionTable;
struct TTEntry;

constexpr inline std::uint8_t DefaultMultiPV = 1;

namespace Search {

// Stack struct keeps track of the information need to remember from nodes
// shallower and deeper in the tree during the search. Each search thread has
// its own array of Stack objects, indexed by the current ply.
struct Stack final {
    Moves            pv;
    PieceDstHistory* continuationHistory;
    std::int16_t     ply;
    Move             currentMove;
    KillerMoves      killerMoves;
    Value            staticEval;
    int              history;
    bool             inCheck;
    bool             ttHit;
    bool             ttPv;
    std::uint8_t     moveCount;
    std::uint8_t     cutoffCount;
};

// RootMove struct is used for moves at the root of the tree. For each root move
// store a score and a PV (really a refutation in the case of moves which fail low).
// Score is normally set at -VALUE_INFINITE for all non-pv moves.
struct RootMove final {

    RootMove() = default;
    explicit RootMove(Move m) noexcept :
        pv(1, m) {}

    void push_back(Move m) noexcept { pv.push_back(m); }
    void push_front(Move m) noexcept { pv.push_front(m); }
    void append(Move m) noexcept { pv.append(m); }
    void append(Moves::ConstItr begItr, Moves::ConstItr endItr) noexcept {
        pv.append(begItr, endItr);
    }
    void append(const std::initializer_list<Move>& initList) noexcept {  //
        pv.append(initList);
    }
    void append(const Moves& ms) noexcept { pv.append(ms); }
    void pop() noexcept { pv.pop(); }

    //void reserve(std::size_t newSize) noexcept { pv.reserve(newSize); }
    void resize(std::size_t newSize) noexcept { pv.resize(newSize); }
    void clear() noexcept { pv.clear(); }

    auto begin() const noexcept { return pv.begin(); }
    auto end() const noexcept { return pv.end(); }

    auto& front() noexcept { return pv.front(); }
    auto& back() noexcept { return pv.back(); }

    auto size() const noexcept { return pv.size(); }
    bool empty() const noexcept { return pv.empty(); }

    bool operator==(Move m) const noexcept { return /*!empty() &&*/ (*this)[0] == m; }
    bool operator!=(Move m) const noexcept { return !(*this == m); }

    bool operator==(const RootMove& rm) const noexcept { return /*!rm.empty() &&*/ *this == rm[0]; }
    bool operator!=(const RootMove& rm) const noexcept { return !(*this == rm); }

    // Sort in descending order
    bool operator<(const RootMove& rm) const noexcept {
        return curValue != rm.curValue ? curValue > rm.curValue
             : preValue != rm.preValue ? preValue > rm.preValue
                                       : avgValue > rm.avgValue;
    }

    Move  operator[](std::size_t idx) const noexcept { return pv[idx]; }
    Move& operator[](std::size_t idx) noexcept { return pv[idx]; }

    void operator+=(Move m) noexcept { pv += m; }
    void operator-=(Move m) noexcept { pv -= m; }

    Value         curValue   = -VALUE_INFINITE;
    Value         preValue   = -VALUE_INFINITE;
    Value         avgValue   = -VALUE_INFINITE;
    Value         uciValue   = -VALUE_INFINITE;
    bool          lowerBound = false;
    bool          upperBound = false;
    std::uint16_t selDepth   = DEPTH_ZERO;
    std::uint64_t nodes      = 0;
    std::int32_t  tbRank     = 0;
    Value         tbValue    = -VALUE_INFINITE;
    Moves         pv;
};

class RootMoves final {
   public:
    using RootMoveVector = std::vector<RootMove>;
    using NormalItr      = RootMoveVector::iterator;
    using ConstItr       = RootMoveVector::const_iterator;

    RootMoves() = default;
    // explicit RootMoves(std::size_t count, const RootMove& rm) noexcept :
    //     rootMoves(count, rm) {}
    // explicit RootMoves(std::size_t count) noexcept :
    //     rootMoves(count) {}
    // RootMoves(const std::initializer_list<RootMove>& initList) noexcept :
    //     rootMoves(initList) {}

    template<typename... Args>
    void emplace(Args&&... args) noexcept {
        rootMoves.emplace_back(std::forward<Args>(args)...);
    }

    void push(const RootMove& rm) noexcept { rootMoves.push_back(rm); }
    void push(RootMove&& rm) noexcept { rootMoves.push_back(std::move(rm)); }
    void pop() noexcept { rootMoves.pop_back(); }

    void reserve(std::size_t newSize) noexcept { rootMoves.reserve(newSize); }
    void resize(std::size_t newSize) noexcept { rootMoves.resize(newSize); }
    void clear() noexcept { rootMoves.clear(); }

    auto begin() noexcept { return rootMoves.begin(); }
    auto end() noexcept { return rootMoves.end(); }

    auto begin() const noexcept { return rootMoves.begin(); }
    auto end() const noexcept { return rootMoves.end(); }

    auto& front() noexcept { return rootMoves.front(); }
    auto& back() noexcept { return rootMoves.back(); }

    auto size() const noexcept { return rootMoves.size(); }
    auto max_size() const noexcept { return rootMoves.max_size(); }
    auto empty() const noexcept { return rootMoves.empty(); }

    auto erase(ConstItr itr) noexcept { return rootMoves.erase(itr); }
    auto erase(ConstItr begItr, ConstItr endItr) noexcept {
        return rootMoves.erase(begItr, endItr);
    }
    bool erase(Move m) noexcept {
        auto itr = find(m);
        if (itr != end())
            return erase(itr), true;
        return false;
    }

    NormalItr find(Move m) noexcept { return std::find(begin(), end(), m); }
    NormalItr find(const RootMove& rm) noexcept { return find(rm[0]); }

    ConstItr find(Move m) const noexcept { return std::find(begin(), end(), m); }
    ConstItr find(const RootMove& rm) const noexcept { return find(rm[0]); }

    ConstItr find(std::size_t begIdx, std::size_t endIdx, Move m) const noexcept {
        assert(begIdx <= endIdx);
        return std::find(begin() + begIdx, begin() + endIdx, m);
    }
    ConstItr find(std::size_t begIdx, std::size_t endIdx, const RootMove& rm) const noexcept {
        return find(begIdx, endIdx, rm[0]);
    }

    bool contains(Move m) const noexcept { return find(m) != end(); }
    bool contains(const RootMove& rm) const noexcept { return contains(rm[0]); }

    bool contains(std::size_t begIdx, std::size_t endIdx, Move m) const noexcept {
        return find(begIdx, endIdx, m) != begin() + endIdx;
    }
    bool contains(std::size_t begIdx, std::size_t endIdx, const RootMove& rm) const noexcept {
        return contains(begIdx, endIdx, rm[0]);
    }

    template<typename Predicate>
    auto find_if(Predicate pred) noexcept {
        return std::find_if(begin(), end(), pred);
    }

    template<typename Predicate>
    void move_to_front(Predicate pred) noexcept {
        auto itr = find_if(pred);
        if (itr != end())
            std::rotate(begin(), itr, itr + 1);
    }

    void swap_to_front(Move m) noexcept {
        auto itr = find(m);
        if (itr != end())
            std::swap(*begin(), *itr);
    }

    void sort(std::size_t begIdx, std::size_t endIdx) noexcept {
        assert(begIdx <= endIdx);
        std::stable_sort(begin() + begIdx, begin() + endIdx);
    }
    template<typename Predicate>
    void sort(Predicate pred) noexcept {
        std::stable_sort(begin(), end(), pred);
    }

    auto& operator[](std::size_t idx) const noexcept { return rootMoves[idx]; }
    auto& operator[](std::size_t idx) noexcept { return rootMoves[idx]; }

   private:
    RootMoveVector rootMoves;
};

// Limits struct stores information sent by GUI about available time to
// search the current move, maximum depth/time, or if in analysis mode.
struct Limits final {

    struct Clock final {
        TimePoint time = 0, inc = 0;
    };

    Limits() = default;

    bool use_time_manager() const noexcept { return clock[WHITE].time || clock[BLACK].time; }

    std::int16_t diff_time(Color stm) const noexcept {
        return (clock[stm].time - clock[~stm].time) / 1000;
    }

    TimePoint                   initialTime = 0;
    std::array<Clock, COLOR_NB> clock{};
    TimePoint                   moveTime  = 0;
    std::uint8_t                movesToGo = 0, mate = 0;
    Depth                       depth    = DEPTH_ZERO;
    std::uint64_t               nodes    = 0;
    std::uint16_t               hitRate  = 512;
    bool                        infinite = false;
    bool                        ponder   = false;
    bool                        perft    = false;
    bool                        detail   = false;
    std::vector<std::string>    searchMoves;
    std::vector<std::string>    ignoreMoves;
};

// Skill structure is used to implement strength limit.
// If have a UCI_ELO, convert it to an appropriate skill level.
// Skill 0 .. 19 now covers CCRL Blitz Elo from 1320 to 3190, approximately
struct Skill final {

    Skill() = default;

    void init(const OptionsMap& options) noexcept;

    bool enabled() const noexcept { return level < MaxLevel; }
    Move best_move() const noexcept { return bestMove; }

    bool time_to_pick(Depth depth) const noexcept { return depth == 1 + int(level); }

    Move
    pick_best_move(const RootMoves& rootMoves, std::uint8_t multiPV, bool pickBest = true) noexcept;

    static constexpr double        MaxLevel = 20.0;
    static constexpr std::uint16_t MinELO   = 1320;
    static constexpr std::uint16_t MaxELO   = 3190;

   private:
    double level    = MaxLevel;
    Move   bestMove = Move::None();
};

// The Engine stores the uci options, networks, thread pool, and transposition table.
// This struct is used to easily forward data to the Search::Worker class.
struct SharedState final {
    SharedState(const OptionsMap&                           optionsMap,
                const NumaReplicated<Eval::NNUE::Networks>& nnueNetworks,
                ThreadPool&                                 threadPool,
                TranspositionTable&                         transpositionTable) noexcept :
        options(optionsMap),
        networks(nnueNetworks),
        threads(threadPool),
        tt(transpositionTable) {}

    const OptionsMap&                           options;
    const NumaReplicated<Eval::NNUE::Networks>& networks;
    ThreadPool&                                 threads;
    TranspositionTable&                         tt;
};

class Worker;

// Null Object Pattern, implement a common interface for the SearchManagers.
// A Null Object will be given to non-mainthread workers.
class ISearchManager {
   public:
    virtual ~ISearchManager()                         = default;
    virtual void should_abort(const Worker&) noexcept = 0;
};

using ISearchManagerPtr = std::unique_ptr<ISearchManager>;

struct EndInfo {
    bool inCheck;
};
struct FullInfo {
    FullInfo(const Position& p, const RootMove& rm) :
        pos(p),
        rootMove(rm) {}
    const Position& pos;
    const RootMove& rootMove;
    Depth           depth;
    Value           value;
    std::uint16_t   multiPV;
    bool            showBound;
    bool            showWDL;
    TimePoint       time;
    std::uint64_t   nodes;
    std::uint16_t   hashfull;
    std::uint64_t   tbHits;
};
struct IterInfo {
    Depth         depth;
    Move          currMove;
    std::uint16_t currMoveNumber;
};
struct MoveInfo {
    Move bestMove;
    Move ponderMove;
};

using OnUpdateEnd  = std::function<void(const EndInfo&)>;
using OnUpdateFull = std::function<void(const FullInfo&)>;
using OnUpdateIter = std::function<void(const IterInfo&)>;
using OnUpdateMove = std::function<void(const MoveInfo&)>;

struct UpdateContext final {
    OnUpdateEnd  onUpdateEnd;
    OnUpdateFull onUpdateFull;
    OnUpdateIter onUpdateIter;
    OnUpdateMove onUpdateMove;
};

// MainSearchManager manages the search from the main thread.
// It is responsible for keeping track of the time,
// and storing data strictly related to the main thread.
class MainSearchManager final: public ISearchManager {
   public:
    MainSearchManager(const UpdateContext& updateCxt) noexcept :
        updateContext(updateCxt) {}

    void should_abort(const Worker& worker) noexcept override;

    void clear(std::uint16_t threadCount) noexcept;

    TimePoint elapsed() const noexcept;
    TimePoint elapsed(const Worker& worker) const noexcept;

    void info_pv(const Worker& worker, Depth depth) const noexcept;

    const UpdateContext& updateContext;

    TimeManager          timeManager;
    PolyBook             polyBook;
    Skill                skill;
    std::int16_t         callsCount    = 0;
    bool                 stopPonderhit = false;
    std::atomic_bool     ponder        = false;
    std::array<Value, 4> iterBestValue;

    Value  prevBestValue;
    Value  prevBestAvgValue;
    double prevTimeReduction;
};

class NullSearchManager final: public ISearchManager {
   public:
    NullSearchManager() = default;
    void should_abort(const Worker&) noexcept override {}
};

// Worker is the class that does the actual search.
// It is instantiated once per thread, and it is responsible for keeping track
// of the search history, and storing data required for the search.
class Worker final {
   public:
    Worker(std::uint16_t             threadId,
           const SharedState&        sharedState,
           ISearchManagerPtr         searchManager,
           NumaReplicatedAccessToken token) noexcept;

    // Called at instantiation to reset histories, usually before a new game
    void clear() noexcept;

    // Called when the program receives the UCI 'go' command.
    // It searches from the root position and outputs the "bestmove".
    void start_search() noexcept;

    // Public because they need to be updatable by the stats
    CounterMoveHistory                      counterMoves;
    ButterflyHistory                        mainHistory;
    CapturePieceDstHistory                  captureHistory;
    std::array2d<ContinuationHistory, 2, 2> continuationHistory;
    PawnHistory                             pawnHistory;
    CorrectionHistory                       correctionHistory;

   private:
    bool is_main_worker() const noexcept { return threadIdx == 0; }

    MainSearchManager* main_manager() const noexcept;

    void iterative_deepening() noexcept;

    // Main search function for both PV and non-PV nodes
    template<bool PVNode>
    // clang-format off
    Value
    search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode, Move excludedMove = Move::None()) noexcept;
    // clang-format on

    // Quiescence search function, which is called by the main search
    template<bool PVNode>
    Value
    qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth = DEPTH_ZERO) noexcept;

    Move extract_tt_move(const Position& pos, const TTEntry* tte) noexcept;
    bool extract_ponder_move() noexcept;

    Limits limits;

    std::uint8_t  multiPV = DefaultMultiPV;
    std::uint8_t  pvIndex, pvLast;
    std::uint16_t selDepth;
    std::uint16_t minNmpPly;

    std::atomic_uint64_t nodes, tbHits;
    std::atomic_uint8_t  bestMoveChange;

    Position  rootPos;
    StateInfo rootState;
    RootMoves rootMoves;
    Depth     rootDepth, completedDepth;
    Value     rootDelta;

    Tablebases::Config tbConfig;

    std::array<Value, COLOR_NB> optimism;

    const std::uint16_t threadIdx;

    // The main thread has a MainSearchManager, the others have a NullSearchManager
    ISearchManagerPtr manager;

    NumaReplicatedAccessToken numaAccessToken;

    const OptionsMap&                           options;
    const NumaReplicated<Eval::NNUE::Networks>& networks;
    ThreadPool&                                 threads;
    TranspositionTable&                         tt;
    // Used by NNUE
    Eval::NNUE::AccumulatorCaches accCaches;

    friend class DON::ThreadPool;
    friend class MainSearchManager;
};

}  // namespace Search
}  // namespace DON

#endif  // #ifndef SEARCH_H_INCLUDED

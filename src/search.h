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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "misc.h"
#include "movepick.h"
#include "polybook.h"
#include "position.h"
#include "score.h"
#include "timeman.h"
#include "types.h"
#include "nnue/nnue_accumulator.h"
#include "syzygy/tbprobe.h"

namespace DON {

class OptionsMap;
class ThreadPool;
class TranspositionTable;

namespace Eval::NNUE {
struct Networks;
}

namespace Search {

using Moves = std::vector<Move>;

// Different node types, used as a template parameter
enum NodeType : std::uint8_t {
    NonPV,
    PV,
    Root
};

// Stack struct keeps track of the information need to remember from nodes
// shallower and deeper in the tree during the search. Each search thread has
// its own array of Stack objects, indexed by the current ply.
struct Stack final {
    Move*            pv;
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

    explicit RootMove(Move m) noexcept :
        pv(1, m) {}

    bool operator==(Move m) const noexcept { return pv[0] == m; }
    bool operator!=(Move m) const noexcept { return !(*this == m); }
    // Sort in descending order
    bool operator<(const RootMove& rm) const noexcept {
        return value != rm.value ? value > rm.value : prevValue > rm.prevValue;
    }

    void push(Move m) noexcept { pv.push_back(m); }

    bool extract_ponder_from_tt(Position& pos, const TranspositionTable& tt) noexcept;

    auto  size() const noexcept { return pv.size(); }
    auto  empty() const noexcept { return pv.empty(); }
    Move  operator[](std::uint8_t idx) const noexcept { return pv[idx]; }
    Move& operator[](std::uint8_t idx) noexcept { return pv[idx]; }

    Value         value      = -VALUE_INFINITE;
    Value         prevValue  = -VALUE_INFINITE;
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

using RootMoves = std::vector<RootMove>;

// Limits struct stores information sent by GUI about available time to
// search the current move, maximum depth/time, or if in analysis mode.
struct Limits final {

    struct Clock final {
        TimePoint time = 0, inc = 0;
    };

    Limits() = default;

    bool use_time_management() const noexcept { return clock[WHITE].time || clock[BLACK].time; }

    std::int16_t time_diff(Color stm) const noexcept {
        return (clock[stm].time - clock[~stm].time) / 1000;
    }

    TimePoint                   startTime = 0;
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

    bool time_to_pick(Depth depth) const noexcept { return depth == 1 + int(level); }

    Move pick_best_move(const RootMoves& rootMoves, std::uint8_t multiPV) noexcept;

    static constexpr double        MaxLevel = 20.0;
    static constexpr std::uint16_t MinELO   = 1320;
    static constexpr std::uint16_t MaxELO   = 3190;

    double level    = MaxLevel;
    Move   bestMove = Move::None();
};

// The Engine stores the uci options, networks, thread pool, and transposition table.
// This struct is used to easily forward data to the Search::Worker class.
struct SharedState final {
    SharedState(const OptionsMap&           optionsMap,
                const Eval::NNUE::Networks& nnueNetworks,
                ThreadPool&                 threadPool,
                TranspositionTable&         transpositionTable) noexcept :
        options(optionsMap),
        networks(nnueNetworks),
        threads(threadPool),
        tt(transpositionTable) {}

    const OptionsMap&           options;
    const Eval::NNUE::Networks& networks;
    ThreadPool&                 threads;
    TranspositionTable&         tt;
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

struct InfoShort {
    InfoShort(const Position& p, Depth d) :
        pos(p),
        depth(d) {}
    const Position& pos;
    Depth           depth;
};
struct InfoFull: InfoShort {
    InfoFull(const Position& p, Depth d, const RootMove& rm) :
        InfoShort(p, d),
        rootMove(rm) {}
    const RootMove& rootMove;
    Value           value;
    std::uint16_t   multiPV;
    bool            showBound;
    bool            showWDL;
    TimePoint       time;
    std::uint64_t   nodes;
    std::uint16_t   hashfull;
    std::uint64_t   tbHits;
};
struct InfoIteration {
    Depth         depth;
    Move          currMove;
    std::uint16_t currMoveNumber;
};
struct InfoBestMove {
    Move bestMove;
    Move ponderMove;
};

using OnUpdateShort     = std::function<void(const InfoShort&)>;
using OnUpdateFull      = std::function<void(const InfoFull&)>;
using OnUpdateIteration = std::function<void(const InfoIteration&)>;
using OnUpdateBestMove  = std::function<void(const InfoBestMove&)>;

struct UpdateContext final {
    OnUpdateShort     onUpdateShort;
    OnUpdateFull      onUpdateFull;
    OnUpdateIteration onUpdateIteration;
    OnUpdateBestMove  onUpdateBestMove;
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

    TimeManagement       tm;
    Skill                skill;
    PolyBook             polyBook;
    std::int16_t         callsCount    = 0;
    bool                 ponderhitStop = false;
    std::atomic_bool     ponder        = false;
    bool                 reportMinimal = false;
    std::array<Value, 4> iterValue;

    Value  prevBestValue     = -VALUE_INFINITE;
    Value  prevBestAvgValue  = -VALUE_INFINITE;
    double prevTimeReduction = 1.0;
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
    Worker(std::uint16_t      threadId,
           const SharedState& sharedState,
           ISearchManagerPtr  searchManager) noexcept;

    // Called at instantiation to reset histories, usually before a new game
    void clear() noexcept;

    // Called when the program receives the UCI 'go' command.
    // It searches from the root position and outputs the "bestmove".
    void start_search() noexcept;

    // Public because they need to be updatable by the stats
    CounterMoveHistory                 counterMoves;
    ButterflyHistory                   mainHistory;
    CapturePieceDstHistory             captureHistory;
    Array2D<ContinuationHistory, 2, 2> continuationHistory;
    PawnHistory                        pawnHistory;
    CorrectionHistory                  correctionHistory;

   private:
    bool is_main_worker() const noexcept { return threadIdx == 0; }

    MainSearchManager* main_manager() const noexcept;

    void iterative_deepening() noexcept;

    // Main search function for both PV and non-PV nodes
    template<NodeType NT>
    // clang-format off
    Value
    search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode, Move excludedMove = Move::None()) noexcept;
    // clang-format on

    // Quiescence search function, which is called by the main search
    template<NodeType NT>
    Value
    qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth = DEPTH_ZERO) noexcept;

    Limits limits;

    std::uint8_t  pvIndex, pvLast;
    std::uint16_t selDepth;
    std::uint16_t nmpMinPly;

    std::atomic_uint64_t nodes, tbHits;
    std::atomic_uint32_t bestMoveChanges;

    Position      rootPos;
    StateInfo     rootState;
    RootMoves     rootMoves;
    Depth         rootDepth, completedDepth;
    std::uint16_t rootDelta;

    Tablebases::Config tbConfig;

    std::array<Value, COLOR_NB> optimism;

    const std::uint16_t threadIdx;

    // The main thread has a MainSearchManager, the others have a NullSearchManager
    ISearchManagerPtr manager;

    const OptionsMap&           options;
    const Eval::NNUE::Networks& networks;
    ThreadPool&                 threads;
    TranspositionTable&         tt;
    // Used by NNUE
    Eval::NNUE::AccumulatorCaches accCaches;

    friend class DON::ThreadPool;
    friend class MainSearchManager;
};

}  // namespace Search
}  // namespace DON

#endif  // #ifndef SEARCH_H_INCLUDED

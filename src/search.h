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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "history.h"
#include "movegen.h"
#include "movepick.h"
#include "numa.h"
#include "position.h"
#include "timeman.h"
#include "types.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include "syzygy/tbprobe.h"

namespace DON {

class Options;
class ThreadPool;
class TranspositionTable;

constexpr inline std::uint8_t DEFAULT_MULTI_PV = 1;

// Node type
enum NodeType : std::uint8_t {
    Root = 4,
    PV   = 2,
    Cut  = 1,
    All  = 0,
};

constexpr NodeType operator~(NodeType nt) noexcept { return NodeType((int(nt) ^ 1) & 1); }

// Stack struct keeps track of the information need to remember from nodes
// shallower and deeper in the tree during the search. Each search thread has
// its own array of Stack objects, indexed by the current ply.
struct Stack final {
    std::int16_t ply;
    std::uint8_t cutoffCount;
    std::uint8_t moveCount;
    Move         move;
    Value        staticEval;
    int          history;
    bool         inCheck;
    bool         ttM;
    bool         ttPv;
    Moves        pv;

    History<HPieceSq>*            pieceSqHistory;
    CorrectionHistory<CHPieceSq>* pieceSqCorrectionHistory;
};

// RootMove struct is used for moves at the root of the tree. For each root move
// store a score and a PV (really a refutation in the case of moves which fail low).
// Score is normally set at -VALUE_INFINITE for all non-pv moves.
struct RootMove final {
   public:
    RootMove() noexcept = default;
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

    void resize(std::size_t newSize) noexcept { pv.resize(newSize); }
    void clear() noexcept { pv.clear(); }

    auto begin() const noexcept { return pv.begin(); }
    auto end() const noexcept { return pv.end(); }

    auto rbegin() noexcept { return pv.rbegin(); }
    auto rend() noexcept { return pv.rend(); }

    auto& front() noexcept { return pv.front(); }
    auto& back() noexcept { return pv.back(); }

    auto size() const noexcept { return pv.size(); }
    bool empty() const noexcept { return pv.empty(); }

    friend bool operator==(const RootMove& rm, Move m) noexcept {
        return !rm.empty() && rm[0] == m;
    }
    friend bool operator!=(const RootMove& rm, Move m) noexcept { return !(rm == m); }

    friend bool operator==(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !rm1.empty() && !rm2.empty() && rm1[0] == rm2[0];
    }
    friend bool operator!=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm1 == rm2);
    }

    // Sort in descending order
    friend bool operator<(const RootMove& rm1, const RootMove& rm2) noexcept {
        return std::tie(rm1.curValue, rm1.preValue, rm1.avgValue)  //
             > std::tie(rm2.curValue, rm2.preValue, rm2.avgValue);
    }
    friend bool operator>(const RootMove& rm1, const RootMove& rm2) noexcept {  //
        return (rm2 < rm1);
    }
    friend bool operator<=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm1 > rm2);
    }
    friend bool operator>=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm1 < rm2);
    }

    Move  operator[](std::size_t idx) const noexcept { return pv[idx]; }
    Move& operator[](std::size_t idx) noexcept { return pv[idx]; }

    void operator+=(Move m) noexcept { pv += m; }

    Value curValue = -VALUE_INFINITE;
    Value preValue = -VALUE_INFINITE;
    Value uciValue = -VALUE_INFINITE;

    Value    avgValue    = -VALUE_INFINITE;
    SqrValue avgSqrValue = -VALUE_INFINITE * VALUE_INFINITE;

    bool          boundLower = false;
    bool          boundUpper = false;
    std::uint16_t selDepth   = DEPTH_ZERO;
    std::uint64_t nodes      = 0;
    std::int32_t  tbRank     = 0;
    Value         tbValue    = -VALUE_INFINITE;
    Moves         pv;
};

class RootMoves final {
   public:
    using RootMoveDeque = std::deque<RootMove>;
    using NormalItr     = RootMoveDeque::iterator;
    using ConstItr      = RootMoveDeque::const_iterator;

    RootMoves() noexcept = default;

    template<typename... Args>
    auto& emplace(Args&&... args) noexcept {
        return rootMoves.emplace_back(std::forward<Args>(args)...);
    }

    void push(const RootMove& rm) noexcept { rootMoves.push_back(rm); }
    void push(RootMove&& rm) noexcept { rootMoves.push_back(std::move(rm)); }
    void pop() noexcept { rootMoves.pop_back(); }

    void resize(std::size_t newSize) noexcept { rootMoves.resize(newSize); }
    void clear() noexcept { rootMoves.clear(); }

    auto begin() const noexcept { return rootMoves.begin(); }
    auto end() const noexcept { return rootMoves.end(); }
    auto begin() noexcept { return rootMoves.begin(); }
    auto end() noexcept { return rootMoves.end(); }

    auto& front() noexcept { return rootMoves.front(); }
    auto& back() noexcept { return rootMoves.back(); }

    auto size() const noexcept { return rootMoves.size(); }
    auto empty() const noexcept { return rootMoves.empty(); }

    auto erase(ConstItr itr) noexcept { return rootMoves.erase(itr); }
    auto erase(ConstItr begItr, ConstItr endItr) noexcept {
        return rootMoves.erase(begItr, endItr);
    }
    bool erase(Move m) noexcept {
        auto itr = find(m);
        if (itr != end())
        {
            erase(itr);
            return true;
        }
        return false;
    }

    ConstItr find(std::size_t begIdx, std::size_t endIdx, Move m) const noexcept {
        assert(begIdx <= endIdx);
        return std::find(begin() + begIdx, begin() + endIdx, m);
    }
    ConstItr find(std::size_t begIdx, std::size_t endIdx, const RootMove& rm) const noexcept {
        return !rm.empty() ? find(begIdx, endIdx, rm[0]) : begin() + endIdx;
    }

    ConstItr find(Move m) const noexcept { return std::find(begin(), end(), m); }
    ConstItr find(const RootMove& rm) const noexcept { return !rm.empty() ? find(rm[0]) : end(); }

    NormalItr find(Move m) noexcept { return std::find(begin(), end(), m); }
    NormalItr find(const RootMove& rm) noexcept { return !rm.empty() ? find(rm[0]) : end(); }

    bool contains(std::size_t begIdx, std::size_t endIdx, Move m) const noexcept {
        return find(begIdx, endIdx, m) != begin() + endIdx;
    }
    bool contains(std::size_t begIdx, std::size_t endIdx, const RootMove& rm) const noexcept {
        return !rm.empty() && contains(begIdx, endIdx, rm[0]);
    }

    bool contains(Move m) const noexcept { return find(m) != end(); }
    bool contains(const RootMove& rm) const noexcept { return !rm.empty() && contains(rm[0]); }

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
    RootMoveDeque rootMoves;
};

// Limit struct stores information sent by GUI about available time to
// search the current move, maximum depth/time, or if in analysis mode.
struct Limit final {
   public:
    struct Clock final {
        TimePoint time = TimePoint(0), inc = TimePoint(0);
    };

    Limit() noexcept = default;

    bool use_time_manager() const noexcept { return clocks[WHITE].time || clocks[BLACK].time; }

    TimePoint startTime = TimePoint(0);

    std::array<Clock, COLOR_NB> clocks;

    std::uint8_t  movesToGo = 0;
    std::uint8_t  mate      = 0;
    TimePoint     moveTime  = TimePoint(0);
    Depth         depth     = DEPTH_ZERO;
    std::uint64_t nodes     = 0;
    std::uint16_t hitRate   = 512;
    bool          infinite  = false;
    bool          ponder    = false;
    bool          perft = false, detail = false;

    std::deque<std::string> searchMoves, ignoreMoves;
};

// Skill struct is used to implement engine strength limit.
// If UCI_ELO is set, convert it to an appropriate skill level.
// Skill 0..19 covers CCRL Blitz Elo from 1320 to 3190, approximately.
struct Skill final {
   public:
    Skill() noexcept = default;

    void init(const Options& options) noexcept;

    bool enabled() const noexcept { return level < MAX_LEVEL; }

    Move best_move() const noexcept { return bestMove; }

    bool time_to_pick(Depth depth) const noexcept { return depth == 1 + int(level); }

    Move
    pick_best_move(const RootMoves& rootMoves, std::uint8_t multiPV, bool pickBest = true) noexcept;

    static constexpr double        MIN_LEVEL = 00.0;
    static constexpr double        MAX_LEVEL = 20.0;
    static constexpr std::uint16_t MIN_ELO   = 1320;
    static constexpr std::uint16_t MAX_ELO   = 3190;

   private:
    double level    = MAX_LEVEL;
    Move   bestMove = Move::None();
};

// SharedState struct stores the engine options, networks, thread pool, and transposition table.
// It is used to easily forward data to the Worker class.
struct SharedState final {
    SharedState(const Options&                            engOptions,
                const LazyNumaReplicated<NNUE::Networks>& nnueNetworks,
                ThreadPool&                               threadPool,
                TranspositionTable&                       transpositionTable) noexcept :
        options(engOptions),
        networks(nnueNetworks),
        threads(threadPool),
        tt(transpositionTable) {}

    const Options&                            options;
    const LazyNumaReplicated<NNUE::Networks>& networks;
    ThreadPool&                               threads;
    TranspositionTable&                       tt;
};

class Worker;

// Null Object Pattern, implement a common interface for the SearchManagers.
// A Null Object will be given to non-mainthread workers.
class ISearchManager {
   public:
    virtual ~ISearchManager() noexcept = default;

    virtual void check_time(Worker&) noexcept = 0;
};

using ISearchManagerPtr = std::unique_ptr<ISearchManager>;

struct EndInfo final {
    bool inCheck;
};
struct FullInfo final {
    FullInfo(const Position& p, const RootMove& rm) noexcept :
        pos(p),
        rootMove(rm) {}
    const Position& pos;
    const RootMove& rootMove;
    Depth           depth;
    Value           value;
    std::uint16_t   multiPV;
    bool            boundShow;
    bool            wdlShow;
    TimePoint       time;
    std::uint64_t   nodes;
    std::uint16_t   hashfull;
    std::uint64_t   tbHits;
};
struct IterInfo final {
    Depth         depth;
    Move          currMove;
    std::uint16_t currMoveNumber;
};
struct MoveInfo final {
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
    MainSearchManager() noexcept = delete;
    explicit MainSearchManager(const UpdateContext& updateContext) noexcept :
        updateCxt(updateContext) {}

    void init(std::uint16_t threadCount) noexcept;

    void check_time(Worker& worker) noexcept override;

    void load_book(const std::string& bookFile) const noexcept;

    TimePoint elapsed() const noexcept;
    TimePoint elapsed(const ThreadPool& threads) const noexcept;

    void show_pv(Worker& worker, Depth depth) const noexcept;

    const UpdateContext& updateCxt;

    std::uint16_t callsCount    = 0;
    bool          ponder        = false;
    bool          ponderhitStop = false;
    TimeManager   timeManager;
    Skill         skill;

    std::array<Value, 4> iterBestValue;

    bool   first            = true;
    Value  preBestCurValue  = VALUE_ZERO;
    Value  preBestAvgValue  = VALUE_ZERO;
    double preTimeReduction = 1.0;
};

class NullSearchManager final: public ISearchManager {
   public:
    NullSearchManager() noexcept = default;

    void check_time(Worker&) noexcept override {}
};

// Worker is the class that does the actual search.
// It is instantiated once per thread, and it is responsible for keeping track
// of the search history, and storing data required for the search.
class Worker final {
   public:
    Worker() noexcept = delete;
    Worker(std::uint16_t             threadId,
           const SharedState&        sharedState,
           ISearchManagerPtr         searchManager,
           NumaReplicatedAccessToken accessToken) noexcept;

    void init() noexcept;

    void ensure_network_replicated() noexcept;

    // Called when the program receives the UCI 'go' command.
    // It searches from the root position and outputs the "bestmove".
    void start_search() noexcept;

   private:
    bool is_main_worker() const noexcept { return threadIdx == 0; }

    // Get a pointer to the search manager,
    // Only allowed to be called by the main worker.
    MainSearchManager* main_manager() const noexcept {
        assert(is_main_worker());
        return static_cast<MainSearchManager*>(manager.get());
    }

    void iterative_deepening() noexcept;

    // clang-format off
    // Main search function for NodeType nodes
    template<NodeType NT>
    Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, Move excludedMove = Move::None()) noexcept;

    // Quiescence search function, which is called by the main search
    template<bool PVNode>
    Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta) noexcept;
    // clang-format on

    Move extract_tt_move(const Position& pos, Move ttMove) const noexcept;
    bool ponder_move_extracted() noexcept;

    Limit              limit;
    Tablebases::Config tbConfig;

    Position  rootPos;
    State     rootState;
    RootMoves rootMoves;
    Depth     rootDepth, completedDepth;

    std::uint64_t nodes, tbHits;
    std::uint16_t moveChanges;

    int           rootDelta;
    std::int16_t  nmpMinPly;
    std::uint8_t  multiPV;
    std::uint8_t  curIdx, fstIdx, lstIdx;
    std::uint16_t selDepth;

    std::array<std::int32_t, COLOR_NB> optimism;

    const std::uint16_t threadIdx;

    // The main thread has a MainSearchManager, the others have a NullSearchManager
    ISearchManagerPtr manager;

    const Options&                            options;
    const LazyNumaReplicated<NNUE::Networks>& networks;
    ThreadPool&                               threads;
    TranspositionTable&                       tt;
    // Used by NNUE
    NumaReplicatedAccessToken numaAccessToken;
    NNUE::AccumulatorCaches   accCaches;

    friend class ThreadPool;
    friend class MainSearchManager;
};

}  // namespace DON

#endif  // #ifndef SEARCH_H_INCLUDED

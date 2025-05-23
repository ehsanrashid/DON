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
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "history.h"
#include "misc.h"
#include "movegen.h"
#include "numa.h"
#include "position.h"
#include "timeman.h"
#include "types.h"
#include "nnue/nnue_accumulator.h"
#include "syzygy/tbprobe.h"

namespace DON {

class Options;
class ThreadPool;
class TranspositionTable;
class Worker;

namespace NNUE {
struct Networks;
}

constexpr std::size_t DEFAULT_MULTI_PV = 1;

namespace Search {

void init() noexcept;

}  // namespace Search

// RootMove struct is used for moves at the root of the tree. For each root move
// store a score and a PV (really a refutation in the case of moves which fail low).
// Score is normally set at -VALUE_INFINITE for all non-pv moves.
struct RootMove final {
   public:
    RootMove() noexcept = default;
    explicit RootMove(const Move& m) noexcept :
        pv(1, m) {}

    auto begin() const noexcept { return pv.begin(); }
    auto end() const noexcept { return pv.end(); }
    auto begin() noexcept { return pv.begin(); }
    auto end() noexcept { return pv.end(); }

    auto rbegin() noexcept { return pv.rbegin(); }
    auto rend() noexcept { return pv.rend(); }

    auto& front() noexcept { return pv.front(); }
    auto& back() noexcept { return pv.back(); }

    auto size() const noexcept { return pv.size(); }
    auto empty() const noexcept { return pv.empty(); }

    template<typename... Args>
    auto& emplace_back(Args&&... args) noexcept {
        return pv.emplace_back(std::forward<Args>(args)...);
    }

    void push_back(const Move& m) noexcept { pv.push_back(m); }
    void push_back(Move&& m) noexcept { pv.push_back(std::move(m)); }

    void pop_back() noexcept { pv.pop_back(); }

    void clear() noexcept { pv.clear(); }

    void resize(std::size_t newSize) noexcept { pv.resize(newSize); }

    auto  operator[](std::size_t idx) const noexcept { return pv[idx]; }
    auto& operator[](std::size_t idx) noexcept { return pv[idx]; }

    friend bool operator==(const RootMove& rm, const Move& m) noexcept {
        return !rm.empty() && rm[0] == m;
    }
    friend bool operator!=(const RootMove& rm, const Move& m) noexcept { return !(rm == m); }

    friend bool operator==(const RootMove& rm1, const RootMove& rm2) noexcept {  //
        return !rm1.empty() && !rm2.empty() && rm1[0] == rm2[0];
    }
    friend bool operator!=(const RootMove& rm1, const RootMove& rm2) noexcept {  //
        return !(rm1 == rm2);
    }

    // Sort in descending order
    friend bool operator<(const RootMove& rm1, const RootMove& rm2) noexcept {  //
        return std::tie(rm1.curValue, rm1.preValue, rm1.avgValue)
             > std::tie(rm2.curValue, rm2.preValue, rm2.avgValue);
    }
    friend bool operator>(const RootMove& rm1, const RootMove& rm2) noexcept {  //
        return (rm2 < rm1);
    }
    friend bool operator<=(const RootMove& rm1, const RootMove& rm2) noexcept {  //
        return !(rm1 > rm2);
    }
    friend bool operator>=(const RootMove& rm1, const RootMove& rm2) noexcept {  //
        return !(rm1 < rm2);
    }

    Value curValue = -VALUE_INFINITE;
    Value preValue = -VALUE_INFINITE;
    Value uciValue = -VALUE_INFINITE;

    Value    avgValue    = -VALUE_INFINITE;
    SqrValue avgSqrValue = sign_sqr(-VALUE_INFINITE);

    bool          boundLower = false;
    bool          boundUpper = false;
    std::uint16_t selDepth   = DEPTH_ZERO;
    std::uint64_t nodes      = 0;
    std::int32_t  tbRank     = 0;
    Value         tbValue    = -VALUE_INFINITE;

   private:
    Moves pv;

    friend class ThreadPool;
    friend class Worker;
};

class RootMoves final {
   public:
    using Vector   = std::vector<RootMove>;
    using Itr      = Vector::iterator;
    using ConstItr = Vector::const_iterator;

    RootMoves() noexcept = default;

    auto begin() const noexcept { return rootMoves.begin(); }
    auto end() const noexcept { return rootMoves.end(); }
    auto begin() noexcept { return rootMoves.begin(); }
    auto end() noexcept { return rootMoves.end(); }

    auto& front() noexcept { return rootMoves.front(); }
    auto& back() noexcept { return rootMoves.back(); }

    auto size() const noexcept { return rootMoves.size(); }
    auto empty() const noexcept { return rootMoves.empty(); }

    template<typename... Args>
    auto& emplace_back(Args&&... args) noexcept {
        return rootMoves.emplace_back(std::forward<Args>(args)...);
    }
    template<typename... Args>
    auto& emplace(ConstItr where, Args&&... args) noexcept {
        return rootMoves.emplace(where, std::forward<Args>(args)...);
    }

    void push_back(const RootMove& rm) noexcept { rootMoves.push_back(rm); }
    void push_back(RootMove&& rm) noexcept { rootMoves.push_back(std::move(rm)); }

    void pop_back() noexcept { rootMoves.pop_back(); }

    void clear() noexcept { rootMoves.clear(); }

    void resize(std::size_t newSize) noexcept { rootMoves.resize(newSize); }
    void reserve(std::size_t newCapacity) noexcept { rootMoves.reserve(newCapacity); }

    auto find(std::size_t fst, std::size_t lst, const Move& m) const noexcept {
        assert(fst <= lst);
        return std::find(begin() + fst, begin() + lst, m);
    }
    auto find(std::size_t fst, std::size_t lst, const RootMove& rm) const noexcept {
        return !rm.empty() ? find(fst, lst, rm[0]) : begin() + lst;
    }

    auto find(const Move& m) const noexcept { return std::find(begin(), end(), m); }
    auto find(const RootMove& rm) const noexcept { return !rm.empty() ? find(rm[0]) : end(); }

    auto find(const Move& m) noexcept { return std::find(begin(), end(), m); }
    auto find(const RootMove& rm) noexcept { return !rm.empty() ? find(rm[0]) : end(); }

    template<typename Predicate>
    auto find_if(Predicate pred) noexcept {
        return std::find_if(begin(), end(), pred);
    }

    bool contains(std::size_t fst, std::size_t lst, const Move& m) const noexcept {
        return find(fst, lst, m) != begin() + lst;
    }
    bool contains(std::size_t fst, std::size_t lst, const RootMove& rm) const noexcept {
        return rm.empty() || contains(fst, lst, rm[0]);
    }

    bool contains(const Move& m) const noexcept { return find(m) != end(); }
    bool contains(const RootMove& rm) const noexcept { return rm.empty() || contains(rm[0]); }

    auto remove(const Move& m) noexcept { return std::remove(begin(), end(), m); }
    auto remove(const RootMove& rm) noexcept { return std::remove(begin(), end(), rm); }

    template<typename Predicate>
    auto remove_if(Predicate pred) noexcept {
        return std::remove_if(begin(), end(), pred);
    }

    auto erase(ConstItr where) noexcept { return rootMoves.erase(where); }
    auto erase(ConstItr fst, ConstItr lst) noexcept { return rootMoves.erase(fst, lst); }
    bool erase(const Move& m) noexcept {
        auto itr = find(m);
        if (itr != end())
        {
            erase(itr);
            return true;
        }
        return false;
    }
    bool erase(const RootMove& rm) noexcept {
        auto itr = find(rm);
        if (itr != end())
        {
            erase(itr);
            return true;
        }
        return false;
    }

    template<typename Predicate>
    void move_to_front(Predicate pred) noexcept {
        auto itr = find_if(pred);
        if (itr != end())
            std::rotate(begin(), itr, itr + 1);
    }

    void swap_to_front(const Move& m) noexcept {
        auto itr = find(m);
        if (itr != end())
            std::swap(*begin(), *itr);
    }

    // Sorts within the range [fst, last)
    void sort(std::size_t fst, std::size_t lst) noexcept {
        assert(fst <= lst);
        std::stable_sort(begin() + fst, begin() + lst);
    }
    template<typename Predicate>
    void sort(Predicate pred) noexcept {
        std::stable_sort(begin(), end(), pred);
    }

    auto& operator[](std::size_t idx) const noexcept { return rootMoves[idx]; }
    auto& operator[](std::size_t idx) noexcept { return rootMoves[idx]; }

   private:
    Vector rootMoves;
};

// Limit struct stores information sent by GUI about available time to
// search the current move, maximum depth/time, or if in analysis mode.
struct Limit final {
   public:
    struct Clock final {
        TimePoint time = TimePoint(0);
        TimePoint inc  = TimePoint(0);
    };

    Limit() noexcept {
        startTime = TimePoint(0);

        clocks[WHITE].time = clocks[BLACK].time = TimePoint(0);
        clocks[WHITE].inc = clocks[BLACK].inc = TimePoint(0);

        movesToGo = 0;
        mate      = 0;
        moveTime  = TimePoint(0);
        depth     = DEPTH_ZERO;
        nodes     = 0;
        hitRate   = 512;
        infinite  = false;
        ponder    = false;
        perft = detail = false;
    }

    bool use_time_manager() const noexcept { return clocks[WHITE].time || clocks[BLACK].time; }

    TimePoint startTime;

    std::array<Clock, COLOR_NB> clocks;

    std::uint8_t  movesToGo;
    std::uint8_t  mate;
    TimePoint     moveTime;
    Depth         depth;
    std::uint64_t nodes;
    std::uint16_t hitRate;
    bool          infinite;
    bool          ponder;
    bool          perft, detail;

    std::vector<std::string> searchMoves, ignoreMoves;
};

// Skill struct is used to implement engine strength limit.
// If UCI_ELO is set, convert it to an appropriate skill level.
// Skill 0..19 covers CCRL Blitz Elo from 1320 to 3190, approximately.
struct Skill final {
   public:
    Skill() noexcept :
        level(MaxLevel),
        move(Move::None) {}

    void init(const Options& options) noexcept;

    bool enabled() const noexcept { return level < MaxLevel; }

    bool time_to_pick(Depth depth) const noexcept { return depth == 1 + int(level); }

    Move pick_move(const RootMoves& rootMoves, std::size_t multiPV, bool pick = true) noexcept;

    static constexpr float         MinLevel = 00.0f;
    static constexpr float         MaxLevel = 20.0f;
    static constexpr std::uint16_t MinELO   = 1320;
    static constexpr std::uint16_t MaxELO   = 3190;

   private:
    float level;
    Move  move;
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
    std::size_t     multiPV;
    bool            boundShow;
    bool            wdlShow;
    TimePoint       time;
    std::uint64_t   nodes;
    std::uint16_t   hashFull;
    std::uint64_t   tbHits;
};
struct IterInfo final {
    Depth       depth;
    Move        currMove;
    std::size_t currMoveNumber;
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
        updateCxt(updateContext) {
        init();
    }

    void init() noexcept;

    void check_time(Worker& worker) noexcept override;

    TimePoint elapsed() const noexcept;
    TimePoint elapsed(const ThreadPool& threads) const noexcept;

    void show_pv(Worker& worker, Depth depth) const noexcept;

    const UpdateContext& updateCxt;

    std::uint16_t callsCount;

    bool        ponder;
    bool        ponderhitStop;
    float       sumMoveChanges;
    float       timeReduction;
    Skill       skill;
    TimeManager timeManager;

    bool  moveFirst;
    Value preBestCurValue;
    Value preBestAvgValue;
    float preTimeReduction;
};

class NullSearchManager final: public ISearchManager {
   public:
    NullSearchManager() noexcept = default;

    void check_time(Worker&) noexcept override {}
};

// Node type
enum NodeType : std::uint8_t {
    Root = 6,
    PV   = 2,
    Cut  = 1,
    All  = 0,
};

constexpr NodeType operator~(NodeType nt) noexcept { return NodeType((int(nt) ^ 1) & 1); }

// Stack struct keeps track of the information need to remember from nodes
// shallower and deeper in the tree during the search.
// Each search thread has its own array of Stack objects, indexed by the ply.
struct Stack final {
   public:
    std::int16_t ply;
    bool         inCheck;
    bool         pvHit;
    Move         ttMove;
    Move         move;
    std::uint8_t moveCount;
    std::uint8_t cutoffCount;
    std::uint8_t conseqChecks;
    Value        staticEval;
    int          history;

    Move*                         pv;
    History<HPieceSq>*            pieceSqHistory;
    CorrectionHistory<CHPieceSq>* pieceSqCorrectionHistory;
};

// Worker is the class that does the actual search.
// It is instantiated once per thread, and it is responsible for keeping track
// of the search history, and storing data required for the search.
class Worker final {
   public:
    Worker() noexcept = delete;
    Worker(std::size_t               threadId,
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
    Value search(Position& pos, Stack* const ss, Value alpha, Value beta, Depth depth, std::int8_t red = 0, const Move& excludedMove = Move::None) noexcept;

    // Quiescence search function, which is called by the main search
    template<bool PVNode>
    Value qsearch(Position& pos, Stack* const ss, Value alpha, Value beta) noexcept;
    // clang-format on

    void do_move(Position& pos, const Move& m, State& st, bool check) noexcept;
    void do_move(Position& pos, const Move& m, State& st) noexcept {
        do_move(pos, m, st, pos.check(m));
    }
    void undo_move(Position& pos, const Move& m) noexcept;
    void do_null_move(Position& pos, State& st) noexcept;
    void undo_null_move(Position& pos) noexcept;

    Value evaluate(const Position& pos) noexcept;

    Move extract_tt_move(const Position& pos, Move ttMove, bool deep = true) const noexcept;
    bool ponder_move_extracted() noexcept;

    void extend_tb_pv(std::size_t index, Value& value) noexcept;

    Limit              limit;
    Tablebases::Config tbConfig;

    Position  rootPos;
    State     rootState;
    RootMoves rootMoves;
    Depth     rootDepth, completedDepth;

    std::atomic<std::uint64_t> nodes, tbHits;
    std::atomic<std::uint16_t> moveChanges;

    int           rootDelta;
    std::int16_t  nmpPly;
    std::size_t   multiPV, fstIdx, lstIdx, curIdx;
    std::uint16_t selDepth;

    std::array<std::int32_t, COLOR_NB> optimism;

    const std::size_t threadIdx;

    // The main thread has a MainSearchManager, the others have a NullSearchManager
    ISearchManagerPtr manager;

    const Options&                            options;
    const LazyNumaReplicated<NNUE::Networks>& networks;
    ThreadPool&                               threads;
    TranspositionTable&                       tt;
    // Used by NNUE
    NumaReplicatedAccessToken numaAccessToken;

    NNUE::AccumulatorCaches accCaches;
    NNUE::AccumulatorStack  accStack;

    friend class ThreadPool;
    friend class MainSearchManager;
};

}  // namespace DON

#endif  // #ifndef SEARCH_H_INCLUDED

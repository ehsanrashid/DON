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
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "history.h"
#include "misc.h"
#include "nnue/nnue_accumulator.h"
#include "numa.h"
#include "polybook.h"
#include "position.h"
#include "syzygy/tbbase.h"
#include "timeman.h"
#include "types.h"

namespace DON {

class Options;
class ThreadPool;
class TranspositionTable;

namespace NNUE {
struct Networks;
}

using Moves = std::vector<Move>;
template<std::size_t Size>
using MovesArray = std::array<Moves, Size>;

constexpr std::size_t DEFAULT_MULTI_PV = 1;

extern PolyBook Book;

namespace Search {

void init() noexcept;

}  // namespace Search

// RootMove struct is used for moves at the root of the tree. For each root move
// store a score and a PV (really a refutation in the case of moves which fail low).
// Score is normally set at -VALUE_INFINITE for all non-pv moves.
struct RootMove final {
   public:
    RootMove() noexcept = default;
    explicit RootMove(Move m) noexcept :
        pv(1, m) {}

    bool operator==(Move m) const noexcept { return !pv.empty() && pv.front() == m; }
    bool operator!=(Move m) const noexcept { return !(*this == m); }

    bool operator==(const RootMove& rm) const noexcept {
        return !pv.empty() && !rm.pv.empty() && pv.front() == rm.pv.front();
    }
    bool operator!=(const RootMove& rm) const noexcept { return !(*this == rm); }

    // Sort in descending order
    bool operator<(const RootMove& rm) const noexcept {
        return std::tie(curValue, preValue, avgValue)
             > std::tie(rm.curValue, rm.preValue, rm.avgValue);
    }
    bool operator>(const RootMove& rm) const noexcept { return (rm < *this); }
    bool operator<=(const RootMove& rm) const noexcept { return !(*this > rm); }
    bool operator>=(const RootMove& rm) const noexcept { return !(*this < rm); }

    Value curValue = -VALUE_INFINITE;
    Value preValue = -VALUE_INFINITE;
    Value uciValue = -VALUE_INFINITE;

    Value    avgValue    = -VALUE_INFINITE;
    SqrValue avgSqrValue = sign_sqr(-VALUE_INFINITE);

    bool boundLower = false;
    bool boundUpper = false;

    std::uint16_t selDepth = 0;
    std::uint64_t nodes    = 0;
    std::int32_t  tbRank   = 0;
    Value         tbValue  = -VALUE_INFINITE;

    Moves pv;
};

class RootMoves final {
   public:
    using value_type      = RootMove;
    using container_type  = std::vector<value_type>;
    using size_type       = container_type::size_type;
    using iterator        = container_type::iterator;
    using const_iterator  = container_type::const_iterator;
    using reference       = container_type::reference;
    using const_reference = container_type::const_reference;

    RootMoves() noexcept = default;

    iterator       begin() noexcept { return rootMoves.begin(); }
    const_iterator begin() const noexcept { return rootMoves.begin(); }
    const_iterator cbegin() const noexcept { return rootMoves.cbegin(); }
    iterator       end() noexcept { return rootMoves.end(); }
    const_iterator end() const noexcept { return rootMoves.end(); }
    const_iterator cend() const noexcept { return rootMoves.cend(); }

    [[nodiscard]] reference       front() noexcept { return rootMoves.front(); }
    [[nodiscard]] const_reference front() const noexcept { return rootMoves.front(); }
    [[nodiscard]] reference       back() noexcept { return rootMoves.back(); }
    [[nodiscard]] const_reference back() const noexcept { return rootMoves.back(); }

    [[nodiscard]] bool      empty() const noexcept { return rootMoves.empty(); }
    [[nodiscard]] size_type size() const noexcept { return rootMoves.size(); }
    [[nodiscard]] size_type capacity() const noexcept { return rootMoves.capacity(); }

    template<typename... Args>
    reference emplace_back(Args&&... args) noexcept {
        return rootMoves.emplace_back(std::forward<Args>(args)...);
    }
    template<typename... Args>
    iterator emplace(const_iterator where, Args&&... args) noexcept {
        return rootMoves.emplace(where, std::forward<Args>(args)...);
    }

    void push_back(const value_type& v) { rootMoves.push_back(v); }
    void push_back(value_type&& v) { rootMoves.push_back(std::move(v)); }

    void pop_back() noexcept { rootMoves.pop_back(); }

    void clear() noexcept { rootMoves.clear(); }

    void resize(size_type newSize) noexcept { rootMoves.resize(newSize); }
    void reserve(size_type newCapacity) noexcept { rootMoves.reserve(newCapacity); }

    const_iterator find(size_type beg, size_type end, Move m) const noexcept {
        assert(beg <= end && end <= size());
        return std::find(begin() + beg, begin() + end, m);
    }
    const_iterator find(size_type beg, size_type end, const value_type& v) const noexcept {
        return !v.pv.empty() ? find(beg, end, v.pv[0]) : begin() + end;
    }

    const_iterator find(Move m) const noexcept { return std::find(begin(), end(), m); }
    const_iterator find(const value_type& v) const noexcept {
        return !v.pv.empty() ? find(v.pv[0]) : end();
    }

    iterator find(Move m) noexcept { return std::find(begin(), end(), m); }
    iterator find(const value_type& v) noexcept { return !v.pv.empty() ? find(v.pv[0]) : end(); }

    template<typename Predicate>
    iterator find_if(Predicate&& pred) noexcept {
        return std::find_if(begin(), end(), std::forward<Predicate>(pred));
    }
    template<typename Predicate>
    const_iterator find_if(Predicate&& pred) const noexcept {
        return std::find_if(begin(), end(), std::forward<Predicate>(pred));
    }

    bool contains(size_type beg, size_type end, Move m) const noexcept {
        assert(beg <= end && end <= size());
        return find(beg, end, m) != begin() + end;
    }
    bool contains(size_type beg, size_type end, const value_type& v) const noexcept {
        assert(beg <= end && end <= size());
        return v.pv.empty() || contains(beg, end, v.pv[0]);
    }

    bool contains(Move m) const noexcept { return find(m) != end(); }
    bool contains(const value_type& v) const noexcept { return v.pv.empty() || contains(v.pv[0]); }

    iterator remove(Move m) noexcept { return std::remove(begin(), end(), m); }
    iterator remove(const value_type& v) noexcept { return std::remove(begin(), end(), v); }

    template<typename Predicate>
    iterator remove_if(Predicate&& pred) noexcept {
        // moves kept elements forward; does NOT shrink the vector
        return std::remove_if(begin(), end(), std::forward<Predicate>(pred));
    }

    iterator erase(const_iterator where) noexcept { return rootMoves.erase(where); }
    iterator erase(const_iterator beg, const_iterator end) noexcept {
        return rootMoves.erase(beg, end);
    }
    bool erase(Move m) noexcept {
        auto itr = find(m);
        if (itr == end())
            return false;
        erase(itr);
        return true;
    }
    bool erase(const value_type& v) noexcept {
        auto itr = find(v);
        if (itr == end())
            return false;
        erase(itr);
        return true;
    }

    template<typename Predicate>
    bool move_to_front(Predicate&& pred) noexcept {
        auto itr = find_if(std::forward<Predicate>(pred));
        if (itr == end())
            return false;
        if (itr != begin())
            std::rotate(begin(), itr, itr + 1);
        return true;
    }

    bool swap_to_front(Move m) noexcept {
        auto itr = find(m);
        if (itr == end() || itr == begin())
            return false;
        std::iter_swap(begin(), itr);
        return true;
    }

    // Sorts within the range [beg, end)
    void sort(size_type beg, size_type end) noexcept {
        assert(beg <= end && end <= size());
        std::stable_sort(begin() + beg, begin() + end);
    }
    template<typename Predicate>
    void sort(Predicate&& pred) noexcept {
        std::stable_sort(begin(), end(), std::forward<Predicate>(pred));
    }

    [[nodiscard]] reference operator[](size_type idx) noexcept {  //
        assert(idx < size());
        return rootMoves[idx];
    }
    [[nodiscard]] const_reference operator[](size_type idx) const noexcept {
        assert(idx < size());
        return rootMoves[idx];
    }

    [[nodiscard]] reference       at(size_type idx) { return rootMoves.at(idx); }
    [[nodiscard]] const_reference at(size_type idx) const { return rootMoves.at(idx); }

   private:
    container_type rootMoves;
};

// Limit struct stores information sent by GUI about available time to
// search the current move, maximum depth/time, or if in analysis mode.
struct Limit final {
   public:
    struct Clock final {
        TimePoint time{0};
        TimePoint inc{0};
    };

    constexpr Limit() noexcept = default;

    bool use_time_manager() const noexcept { return clocks[WHITE].time || clocks[BLACK].time; }

    TimePoint startTime{0};

    Clock clocks[COLOR_NB]{};

    std::uint8_t  movesToGo{0};
    std::uint8_t  mate{0};
    TimePoint     moveTime{0};
    Depth         depth{DEPTH_ZERO};
    std::uint64_t nodes{0};
    std::uint16_t hitRate{512};
    bool          infinite{false};
    bool          ponder{false};
    bool          perft{false}, detail{false};

    Strings searchMoves{}, ignoreMoves{};
};

// Skill struct is used to implement engine strength limit.
// If UCI_ELO is set, convert it to an appropriate skill level.
// Skill 0..19 covers CCRL Blitz Elo from 1320 to 3190, approximately.
struct Skill final {
   public:
    constexpr Skill() noexcept = default;

    void init(const Options& options) noexcept;

    bool enabled() const noexcept { return level < MaxLevel; }

    bool time_to_pick(Depth depth) const noexcept { return depth == 1 + int(level); }

    Move pick_move(const RootMoves& rootMoves, std::size_t multiPV, bool pick = true) noexcept;

    static constexpr double        MinLevel = 00.0;
    static constexpr double        MaxLevel = 20.0;
    static constexpr std::uint16_t MinELO   = 1320;
    static constexpr std::uint16_t MaxELO   = 3190;

   private:
    double level{MaxLevel};
    Move   move{Move::None};
};

// SharedState struct stores the engine options, networks, thread pool, and transposition table.
// It is used to easily forward data to the Worker class.
struct SharedState final {
   public:
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

struct ShortInfo {
    Depth            depth;
    std::string_view score;
};
struct FullInfo final: public ShortInfo {
    std::uint16_t    selDepth;
    std::size_t      multiPV;
    std::string_view bound;
    std::string_view wdl;
    TimePoint        time;
    std::uint64_t    nodes;
    std::uint16_t    hashfull;
    std::uint64_t    tbHits;
    std::string_view pv;
};
struct IterInfo final {
    Depth            depth;
    std::string_view currMove;
    std::size_t      currMoveNumber;
};
struct MoveInfo final {
    std::string_view bestMove;
    std::string_view ponderMove;
};

// MainSearchManager manages the search from the main thread.
// It is responsible for keeping track of the time,
// and storing data strictly related to the main thread.
class MainSearchManager final: public ISearchManager {
   public:
    using OnUpdateShort = std::function<void(const ShortInfo&)>;
    using OnUpdateFull  = std::function<void(const FullInfo&)>;
    using OnUpdateIter  = std::function<void(const IterInfo&)>;
    using OnUpdateMove  = std::function<void(const MoveInfo&)>;

    struct UpdateContext final {
        OnUpdateShort onUpdateShort;
        OnUpdateFull  onUpdateFull;
        OnUpdateIter  onUpdateIter;
        OnUpdateMove  onUpdateMove;
    };

    constexpr MainSearchManager() noexcept = delete;
    explicit constexpr MainSearchManager(const UpdateContext& updateContext) noexcept :
        updateCxt(updateContext) {}

    void init() noexcept;

    void check_time(Worker& worker) noexcept override;

    TimePoint elapsed() const noexcept;
    TimePoint elapsed(const ThreadPool& threads) const noexcept;

    void show_pv(Worker& worker, Depth depth) const noexcept;

    const UpdateContext& updateCxt;

    std::uint16_t callsCount{};

    bool        ponder{};
    bool        ponderhitStop{};
    double      sumMoveChanges{};
    double      timeReduction{};
    Skill       skill{};
    TimeManager timeManager{};

    bool   moveFirst{};
    Value  preBestCurValue{};
    Value  preBestAvgValue{};
    double preTimeReduction{};
};

class NullSearchManager final: public ISearchManager {
   public:
    constexpr NullSearchManager() noexcept = default;

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
// Each search thread has its own array of Stack objects, indexed by the ply. (Size = 40)
struct Stack final {
   public:
    Move*                         pv;
    History<HPieceSq>*            pieceSqHistory;
    CorrectionHistory<CHPieceSq>* pieceSqCorrectionHistory;

    int          history;
    Value        staticEval;
    std::int16_t ply;
    Move         move;
    Move         ttMove;
    std::uint8_t moveCount;
    std::uint8_t cutoffCount;
    bool         inCheck;
    bool         pvHit;
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

    // Main search function for NodeType nodes
    template<NodeType NT>
    Value search(Position&    pos,
                 Stack* const ss,
                 Value        alpha,
                 Value        beta,
                 Depth        depth,
                 std::int8_t  red          = 0,
                 Move         excludedMove = Move::None) noexcept;

    // Quiescence search function, which is called by the main search
    template<bool PVNode>
    Value qsearch(Position& pos, Stack* const ss, Value alpha, Value beta) noexcept;

    void do_move(Position& pos, Move m, State& st, bool check, Stack* const ss = nullptr) noexcept;
    void do_move(Position& pos, Move m, State& st, Stack* const ss = nullptr) noexcept;
    void undo_move(Position& pos, Move m) noexcept;
    void do_null_move(Position& pos, State& st, Stack* const ss) noexcept;
    void undo_null_move(Position& pos) noexcept;

    Value evaluate(const Position& pos) noexcept;

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
    std::size_t   multiPV, curIdx, endIdx;
    std::uint16_t selDepth;

    std::int32_t optimism[COLOR_NB];

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

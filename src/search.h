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
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string_view>
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

using MoveVector = std::vector<Move>;

inline constexpr std::size_t MOVE_CAPACITY = 32;
using MoveFixedVector                      = FixedVector<Move, MOVE_CAPACITY>;

inline constexpr std::size_t DEFAULT_MULTI_PV = 1;

inline PolyBook Book;

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

    friend bool operator==(const RootMove& rm, Move m) noexcept {
        return !rm.pv.empty() && rm.pv.front() == m;
    }
    friend bool operator!=(const RootMove& rm, Move m) noexcept { return !(rm == m); }
    friend bool operator==(Move m, const RootMove& rm) noexcept { return (rm == m); }
    friend bool operator!=(Move m, const RootMove& rm) noexcept { return !(rm == m); }

    friend bool operator==(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !rm1.pv.empty() && !rm2.pv.empty() && rm1.pv.front() == rm2.pv.front();
    }
    friend bool operator!=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm1 == rm2);
    }

    // Sort in descending order
    friend bool operator<(const RootMove& rm1, const RootMove& rm2) noexcept {
        return rm1.curValue != rm2.curValue ? rm1.curValue > rm2.curValue
             : rm1.preValue != rm2.preValue ? rm1.preValue > rm2.preValue
                                            : rm1.avgValue > rm2.avgValue;
    }
    friend bool operator>(const RootMove& rm1, const RootMove& rm2) noexcept { return (rm2 < rm1); }
    friend bool operator<=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm2 < rm1);
    }
    friend bool operator>=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm1 < rm2);
    }

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

    MoveVector pv;
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
    // Construct from existing vector
    explicit RootMoves(container_type rms) noexcept :
        rootMoves(std::move(rms)) {}
    // Construct from initializer list
    RootMoves(std::initializer_list<value_type> initList) noexcept :
        rootMoves(initList) {}

    const_iterator begin() const noexcept { return rootMoves.begin(); }
    const_iterator end() const noexcept { return rootMoves.end(); }
    const_iterator cbegin() const noexcept { return rootMoves.cbegin(); }
    const_iterator cend() const noexcept { return rootMoves.cend(); }
    iterator       begin() noexcept { return rootMoves.begin(); }
    iterator       end() noexcept { return rootMoves.end(); }

    [[nodiscard]] const_reference front() const noexcept { return rootMoves.front(); }
    [[nodiscard]] const_reference back() const noexcept { return rootMoves.back(); }
    [[nodiscard]] reference       front() noexcept { return rootMoves.front(); }
    [[nodiscard]] reference       back() noexcept { return rootMoves.back(); }

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

    [[nodiscard]] const_reference operator[](size_type idx) const noexcept {
        assert(idx < size());
        return rootMoves[idx];
    }
    [[nodiscard]] reference operator[](size_type idx) noexcept {  //
        assert(idx < size());
        return rootMoves[idx];
    }

    [[nodiscard]] const_reference at(size_type idx) const { return rootMoves.at(idx); }
    [[nodiscard]] reference       at(size_type idx) { return rootMoves.at(idx); }

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

    std::uint16_t calls_count() const noexcept {
        return nodes ? std::min(1 + int(std::ceil(nodes / 1024.0)), 512) : 512;
    }

    TimePoint startTime{0};

    StdArray<Clock, COLOR_NB> clocks{};

    std::uint8_t  movesToGo{0};
    std::uint8_t  mate{0};
    TimePoint     moveTime{0};
    Depth         depth{DEPTH_ZERO};
    std::uint64_t nodes{0};
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

    Move
    pick_move(const RootMoves& rootMoves, std::size_t multiPV, bool pickActive = true) noexcept;

    static constexpr double        MinLevel = 00.0;
    static constexpr double        MaxLevel = 20.0;
    static constexpr std::uint16_t MinELO   = 1320;
    static constexpr std::uint16_t MaxELO   = 3190;

   private:
    double level{MaxLevel};
    Move   bestMove{Move::None};
};

// SharedState struct stores the engine options, networks, thread pool, and transposition table.
// It is used to easily forward data to the Worker class.
struct SharedState final {
   public:
    SharedState(const Options&                                      engOptions,
                const SystemWideLazyNumaReplicated<NNUE::Networks>& nnueNetworks,
                ThreadPool&                                         threadPool,
                TranspositionTable&                                 transpositionTable) noexcept :
        options(engOptions),
        networks(nnueNetworks),
        threads(threadPool),
        tt(transpositionTable) {}

    const Options&                                      options;
    const SystemWideLazyNumaReplicated<NNUE::Networks>& networks;
    ThreadPool&                                         threads;
    TranspositionTable&                                 tt;
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
    bool          ponder{};
    bool          ponderhitStop{};
    double        sumMoveChanges{};
    double        timeReduction{};
    Skill         skill{};
    TimeManager   timeManager{};

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
    bool         ttPv;
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
    void undo_null_move(Position& pos) const noexcept;

    Value evaluate(const Position& pos) noexcept;

    // clang-format off
    void update_capture_history(Piece pc, Square dst, PieceType captured, int bonus) noexcept;
    void update_capture_history(const Position& pos, Move m, int bonus) noexcept;
    void update_quiet_history(Color ac, Move m, int bonus) noexcept;
    void update_pawn_history(std::uint16_t pawnIndex, Piece pc, Square dst, int bonus) noexcept;
    void update_low_ply_quiet_history(std::int16_t ssPly, Move m, int bonus) noexcept;

    void update_quiet_histories(const Position& pos, Stack* const ss, std::uint16_t pawnIndex, Move m, int bonus) noexcept;
    void update_histories(const Position& pos, Stack* const ss, std::uint16_t pawnIndex, Depth depth, Move bm, const StdArray<MoveFixedVector, 2>& worseMoves) noexcept;

    void update_correction_history(const Position& pos, Stack* const ss, int bonus) noexcept;
    int  correction_value(const Position& pos, const Stack* const ss) noexcept;
    // clang-format on

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

    // History
    History<HCapture>     captureHistory;
    History<HQuiet>       quietHistory;
    History<HPawn>        pawnHistory;
    History<HLowPlyQuiet> lowPlyQuietHistory;

    StdArray<History<HContinuation>, 2, 2> continuationHistory;  // [inCheck][capture]

    History<HTTMove> ttMoveHistory;

    // Correction History
    CorrectionHistory<CHPawn>         pawnCorrectionHistory;
    CorrectionHistory<CHMinor>        minorCorrectionHistory;
    CorrectionHistory<CHNonPawn>      nonPawnCorrectionHistory;
    CorrectionHistory<CHContinuation> continuationCorrectionHistory;

    StdArray<std::int32_t, COLOR_NB> optimism;

    const std::size_t threadIdx;

    // The main thread has a MainSearchManager, the others have a NullSearchManager
    ISearchManagerPtr manager;

    const Options&                                      options;
    const SystemWideLazyNumaReplicated<NNUE::Networks>& networks;
    ThreadPool&                                         threads;
    TranspositionTable&                                 tt;
    // Used by NNUE
    NumaReplicatedAccessToken numaAccessToken;

    NNUE::AccumulatorStack  accStack;
    NNUE::AccumulatorCaches accCaches;

    friend class ThreadPool;
    friend class MainSearchManager;
};

}  // namespace DON

#endif  // #ifndef SEARCH_H_INCLUDED

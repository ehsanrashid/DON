/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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
#include <array>  // IWYU pragma: keep
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "history.h"
#include "misc.h"
#include "notation.h"
#include "numa.h"
#include "position.h"
#include "timeman.h"
#include "types.h"
#include "book/polyglot.h"
#include "nnue/accumulator.h"
#include "tablebase/syzygy.h"

namespace DON {

class Options;
class Threads;
class TranspositionTable;

namespace NNUE {
class Network;
}

// Maximum number of moves stored during search
inline constexpr usize MOVES_CAPACITY = 32;

using MoveVector = FixedVector<Move, MOVES_CAPACITY, u16>;

inline Book::PolyGlot pgBook;

struct PVMoves final {
   public:
    Move*       begin() noexcept { return data(); }
    const Move* begin() const noexcept { return data(); }

    Move*       end() noexcept { return data() + size(); }
    const Move* end() const noexcept { return data() + size(); }

    Move&       front() noexcept { return moves_.front(); }
    const Move& front() const noexcept { return moves_.front(); }

    Move& operator[](usize idx) noexcept {
        assert(idx < size());
        return moves_[idx];
    }
    const Move& operator[](usize idx) const noexcept {
        assert(idx < size());
        return moves_[idx];
    }

    usize size() const noexcept { return size_; }
    bool  empty() const noexcept { return size() == 0; }

    [[nodiscard]] constexpr usize capacity() const noexcept { return moves_.size(); }

    [[nodiscard]] Move*       data() noexcept { return moves_.data(); }
    [[nodiscard]] const Move* data() const noexcept { return moves_.data(); }

    void clear() noexcept { size_ = 0; }

    void push_back(const Move move) noexcept {
        assert(size() < capacity());

        moves_[size_++] = move;
    }

    void shrink_to(const usize newSize) noexcept {
        assert(newSize <= size());

        size_ = newSize;
    }

    // Appends move and child-Pv[]
    void update(Move m, const PVMoves* childPv) noexcept {
        assert(childPv == nullptr || childPv->size() < capacity());

        clear();

        push_back(m);

        if (childPv != nullptr)
        {
            const auto childPvSize = childPv->size();

            std::memcpy(end(), childPv->data(), childPvSize * sizeof(Move));
            size_ += childPvSize;
        }
    }

    // Optimized PV to string conversion
    std::string build_pv() const noexcept {
        std::string pvStr;
        pvStr.reserve(6 * size());

        for (const Move m : *this)
        {
            pvStr.push_back(' ');
            pvStr.append(move_to_can(m));
        }

        return pvStr;
    }

   private:
    Array<Move, PLY_MAX + 1> moves_;
    usize                    size_ = 0;
};

// RootMove is used for moves at the root of the tree.
// For each root move store a score and a PV
// (really a refutation in the case of moves which fail low).
// Score is normally set at -VALUE_INFINITE for all non-pv moves.
struct RootMove final {
   public:
    RootMove() noexcept = default;
    explicit RootMove(Move m) noexcept { push_back(m); }

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
        return rm1.value != rm2.value ? rm1.value > rm2.value : rm1.preValue > rm2.preValue;
    }
    friend bool operator>(const RootMove& rm1, const RootMove& rm2) noexcept { return (rm2 < rm1); }
    friend bool operator<=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm2 < rm1);
    }
    friend bool operator>=(const RootMove& rm1, const RootMove& rm2) noexcept {
        return !(rm1 < rm2);
    }

    [[nodiscard]] bool is_bound() const noexcept {
        return bound == Bound::LOWER || bound == Bound::UPPER;
    }
    [[nodiscard]] bool is_exact_loss() const noexcept {
        return value != -VALUE_INFINITE && is_loss(value) && !is_bound();
    }

    void reset_bound() noexcept { bound = Bound::NONE; }

    [[nodiscard]] usize size() const noexcept { return pv.size(); }
    [[nodiscard]] bool  empty() const noexcept { return size() == 0; }

    void push_back(const Move move) noexcept { pv.push_back(move); }
    // Keep only the root move at index 0
    void shrink_to(const usize newSize) noexcept { pv.shrink_to(newSize); }

    Move&       operator[](const usize idx) noexcept { return pv[idx]; }
    const Move& operator[](const usize idx) const noexcept { return pv[idx]; }

    u64      nodes       = 0;
    Value    value       = -VALUE_INFINITE;
    Value    preValue    = -VALUE_INFINITE;
    Value    avgValue    = -VALUE_INFINITE;
    SqrValue avgSqrValue = sign_sqr(-VALUE_INFINITE);
    Value    uciValue    = -VALUE_INFINITE;
    Bound    bound       = Bound::NONE;
    i32      tbRank      = 0;
    Value    tbValue     = -VALUE_INFINITE;
    u16      id          = std::numeric_limits<u16>::max();
    u16      selDepth    = 0;
    bool     isExact     = false;

    PVMoves pv, prePV;
};

constexpr bool root_move_descending(const RootMove& rm1, const RootMove& rm2) noexcept {
    return rm1.tbRank > rm2.tbRank;
}

// RootMoves a container for RootMove objects, providing utility methods
class RootMoves final {
   public:
    using value_type      = RootMove;
    using container_type  = std::vector<value_type>;
    using size_type       = container_type::size_type;
    using iterator        = container_type::iterator;
    using const_iterator  = container_type::const_iterator;
    using reference       = container_type::reference;
    using const_reference = container_type::const_reference;

    RootMoves() noexcept { reserve(32); }
    explicit RootMoves(const container_type& rms) noexcept :
        rootMoves_(rms) {}
    explicit RootMoves(container_type&& rms) noexcept :
        rootMoves_(std::move(rms)) {}
    RootMoves(std::initializer_list<value_type> initList) :
        rootMoves_(initList) {}

    [[nodiscard]] size_type capacity() const noexcept { return rootMoves_.capacity(); }

    [[nodiscard]] bool      empty() const noexcept { return rootMoves_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return rootMoves_.size(); }

    iterator                     begin() noexcept { return rootMoves_.begin(); }
    iterator                     end() noexcept { return rootMoves_.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return rootMoves_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return rootMoves_.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return rootMoves_.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return rootMoves_.cend(); }

    [[nodiscard]] reference       front() noexcept { return rootMoves_.front(); }
    [[nodiscard]] reference       back() noexcept { return rootMoves_.back(); }
    [[nodiscard]] const_reference front() const noexcept { return rootMoves_.front(); }
    [[nodiscard]] const_reference back() const noexcept { return rootMoves_.back(); }

    template<typename... Args>
    reference emplace_back(Args&&... args) noexcept {
        return rootMoves_.emplace_back(std::forward<Args>(args)...);
    }
    template<typename... Args>
    iterator emplace(const_iterator where, Args&&... args) noexcept {
        return rootMoves_.emplace(where, std::forward<Args>(args)...);
    }

    void push_back(const value_type& v) { rootMoves_.push_back(v); }
    void push_back(value_type&& v) { rootMoves_.push_back(std::move(v)); }

    void pop_back() noexcept { rootMoves_.pop_back(); }

    void clear() noexcept { rootMoves_.clear(); }

    void resize(size_type newSize) noexcept { rootMoves_.resize(newSize); }
    void reserve(size_type newCapacity) noexcept { rootMoves_.reserve(newCapacity); }

    [[nodiscard]] const_iterator  //
    find(size_type beg, size_type end, Move m) const noexcept {
        assert(beg <= end && end <= size());
        return std::find(begin() + beg, begin() + end, m);
    }
    [[nodiscard]] const_iterator  //
    find(size_type beg, size_type end, const value_type& v) const noexcept {
        return !v.pv.empty() ? find(beg, end, v.pv[0]) : begin() + end;
    }

    iterator find(Move m) noexcept { return std::find(begin(), end(), m); }
    iterator find(const value_type& v) noexcept { return !v.pv.empty() ? find(v.pv[0]) : end(); }

    [[nodiscard]] const_iterator find(Move m) const noexcept {
        return std::find(begin(), end(), m);
    }
    [[nodiscard]] const_iterator find(const value_type& v) const noexcept {
        return !v.pv.empty() ? find(v.pv[0]) : end();
    }

    template<typename Predicate>
    iterator find_if(Predicate&& pred) noexcept {
        return std::find_if(begin(), end(), std::forward<Predicate>(pred));
    }
    template<typename Predicate>
    const_iterator find_if(Predicate&& pred) const noexcept {
        return std::find_if(begin(), end(), std::forward<Predicate>(pred));
    }

    [[nodiscard]] bool contains(size_type beg, size_type end, Move m) const noexcept {
        assert(beg <= end && end <= size());

        auto fst = begin() + beg;
        auto lst = begin() + end;

        return std::find(fst, lst, m) != lst;
    }
    [[nodiscard]] bool contains(size_type beg, size_type end, const value_type& v) const noexcept {
        assert(beg <= end && end <= size());

        return v.pv.empty() || contains(beg, end, v.pv[0]);
    }

    [[nodiscard]] bool contains(Move m) const noexcept { return find(m) != end(); }
    [[nodiscard]] bool contains(const value_type& v) const noexcept {
        return v.pv.empty() || contains(v.pv[0]);
    }

    iterator remove(Move m) noexcept { return std::remove(begin(), end(), m); }
    iterator remove(const value_type& v) noexcept { return std::remove(begin(), end(), v); }

    template<typename Predicate>
    iterator remove_if(Predicate&& pred) noexcept {
        // moves kept elements forward; does NOT shrink the vector
        return std::remove_if(begin(), end(), std::forward<Predicate>(pred));
    }

    iterator erase(const_iterator where) noexcept { return rootMoves_.erase(where); }
    iterator erase(const_iterator beg, const_iterator end) noexcept {
        return rootMoves_.erase(beg, end);
    }

    bool erase(Move m) noexcept {
        auto newEnd  = remove(m);
        bool removed = newEnd != end();
        erase(newEnd, end());
        return removed;
    }
    bool erase(const value_type& v) noexcept {
        auto newEnd  = remove(v);
        bool removed = newEnd != end();
        erase(newEnd, end());
        return removed;
    }

    template<typename Predicate>
    bool erase_if(Predicate&& pred) noexcept {
        auto newEnd  = remove_if(std::forward<Predicate>(pred));
        bool removed = newEnd != end();
        erase(newEnd, end());
        return removed;
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
        // Nothing to swap or already at front
        if (itr == begin() || itr == end())
            return false;

        std::iter_swap(begin(), itr);
        return true;
    }

    void sort(size_type beg, size_type end) noexcept {
        assert(beg <= end && end <= size());
        std::stable_sort(begin() + beg, begin() + end);
    }
    template<typename Predicate>
    void sort(size_type beg, size_type end, Predicate&& pred) noexcept {
        assert(beg <= end && end <= size());
        std::stable_sort(begin() + beg, begin() + end, std::forward<Predicate>(pred));
    }
    template<typename Predicate>
    void sort(Predicate&& pred) noexcept {
        std::stable_sort(begin(), end(), std::forward<Predicate>(pred));
    }

    [[nodiscard]] reference operator[](size_type idx) noexcept {
        assert(idx < size());
        return rootMoves_[idx];
    }
    [[nodiscard]] const_reference operator[](size_type idx) const noexcept {
        assert(idx < size());
        return rootMoves_[idx];
    }

    [[nodiscard]] reference       at(size_type idx) { return rootMoves_.at(idx); }
    [[nodiscard]] const_reference at(size_type idx) const { return rootMoves_.at(idx); }

   private:
    container_type rootMoves_;
};

// Limit encapsulates various search limits and time controls, including per-color clocks
struct Limit final {
   public:
    struct Clock final {
       public:
        TimePoint time = 0;
        TimePoint inc  = 0;
    };

    constexpr bool use_time_manager() const noexcept {
        return clocks[WHITE].time != 0 || clocks[BLACK].time != 0;
    }

    constexpr u16 calls_count() const noexcept {
        return nodes != 0 ? std::min(constexpr_ceil(static_cast<double>(nodes) / KB) + 1, 512)
                          : 512;
    }

    TimePoint startTime = 0;

    Array<Clock, COLOR_NB> clocks{};

    Strings searchMoves, ignoreMoves;

    u64       nodes     = 0;
    TimePoint moveTime  = 0;
    Depth     depth     = DEPTH_ZERO;
    u8        movesToGo = 0;
    u8        mate      = 0;
    bool      infinite  = false;
    bool      ponder    = false;
    bool      perft = false, detail = false;
};

// Skill is used to implement engine strength limit.
// If UCI_ELO is set, convert it to an appropriate skill level.
// Skill 0...19.99 covers CCRL Blitz Elo from 1320...3190, approximately.
struct Skill final {
   public:
    constexpr Skill() noexcept = default;

    void init(const Options& options) noexcept;

    [[nodiscard]] constexpr bool enabled() const noexcept { return level < LEVEL_MAX; }

    [[nodiscard]] constexpr bool time_to_pick(Depth depth) const noexcept {
        return depth == 1 + int(level);
    }

    [[nodiscard]] constexpr Value weakness() const noexcept {
        return Value(2.0 * (3.0 * LEVEL_MAX - level));
    }

    Move pick_move(const RootMoves& rootMoves, usize multiPV, bool pickBest = true) noexcept;

    static constexpr double LEVEL_MIN = 00.0;
    static constexpr double LEVEL_MAX = 20.0;

    static constexpr u16 ELO_MIN = 1320;
    static constexpr u16 ELO_MAX = 3190;

   private:
    double level    = LEVEL_MAX;
    Move   bestMove = Move::None;
};

// SharedState stores the shared resources.
// It is used to easily forward data to the Worker class.
struct SharedState final {
   public:
    const SystemWideLazyNumaReplicated<NNUE::Network>& network;
    const Options&                                     options;
    const TranspositionTable&                          transpositionTable;
    Threads&                                           threads;
    SharedHistoriesMap&                                sharedHistoriesMap;
};

class Worker;

// Null Object Pattern, implement a common interface for the SearchManagers.
// Null Object will be given to non-main-thread workers.
class ISearchManager {
   public:
    virtual ~ISearchManager() noexcept = default;

    virtual void check_time(Worker&) noexcept = 0;
};

// Define a unique pointer type for ISearchManager
using ISearchManagerPtr = std::unique_ptr<ISearchManager>;

struct ShortInfo {
   public:
    Depth     depth;
    FixedText score;
};
struct FullInfo final: public ShortInfo {
   public:
    u16              selDepth;
    usize            multiPV;
    FixedText        bound;
    FixedText        wdl;
    TimePoint        time;
    u64              nodes;
    u64              tbHits;
    u16              hashfull;
    std::string_view pv;
};
struct IterInfo final {
   public:
    Depth            depth;
    std::string_view currMove;
    usize            currMoveNumber;
};
struct MoveInfo final {
   public:
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

    MainSearchManager() noexcept = delete;
    explicit MainSearchManager(const UpdateContext& updateCtx) noexcept;

    void reset() noexcept;

    void check_time(Worker& worker) noexcept override;

    [[nodiscard]] TimePoint elapsed() const noexcept;
    [[nodiscard]] TimePoint elapsed(const Threads& threads) const noexcept;

    void
    handle_time_management(const Worker& worker, Value bestValue, Depth lastBestMoveDepth) noexcept;

    void show_pv(Worker& worker, Depth depth) const noexcept;

    void set_ponder(bool p) noexcept;

    const UpdateContext& updateContext;

    Skill       skill;
    TimeManager timeManager;
    double      sumMoveChanges;
    double      timeReduction;
    u16         callsCount;
    bool        pvShown;
    bool        ponder;
    bool        ponderhitStop;

    Value  preBestValue;
    Value  preBestAvgValue;
    double preTimeReduction;
    bool   atFirst;

    std::mutex              mutex;
    std::condition_variable condVar;
};

// NullSearchManager is a no-op implementation of ISearchManager
class NullSearchManager final: public ISearchManager {
   public:
    void check_time(Worker&) noexcept override {}
};

// NT indicates the type of node in the search tree
enum class NT : u8 {
    ALL  = 0,
    CUT  = 1,
    PV   = 2,
    ROOT = 6,
};

constexpr NT operator~(NT nt) noexcept { return NT((static_cast<u8>(nt) ^ 1) & 1); }

// Stack keeps track of the information need to remember from nodes
// shallower and deeper in the tree during the search.
// Each search thread has its own array of Stack objects, indexed by the ply. (Size = 40)
struct Stack final {
   public:
    PVMoves*                  pv;
    PieceSqHistory*           pieceSqHistory;
    PieceSqCorrectionHistory* pieceSqCorrectionHistory;

    int   history;
    Value evalue;
    i16   ply;
    Move  move;
    Move  ttMove;
    u16   moveCount;
    u16   cutoffCount;
    bool  inCheck;
    bool  pvTT;
    bool  pvFollow;
};

// Worker does the actual search.
// It is instantiated once per thread, and it is responsible for keeping track
// of the search history, and storing data required for the search.
class Worker final {
   public:
    Worker() noexcept = delete;
    Worker(usize                     threadIdx,
           usize                     threadCnt,
           usize                     numaIdx,
           usize                     numaThreadCnt,
           NumaReplicatedAccessToken accessToken,
           ISearchManagerPtr         searchManager,
           const SharedState&        sharedState) noexcept;

    void reset() noexcept;

    void ensure_network_replicated() const noexcept;

    // Called when the program receives the UCI 'go' command.
    // It searches from the root position and outputs the "bestmove".
    void start_search() noexcept;

    constexpr usize thread_id() const noexcept { return threadId; }

    constexpr usize thread_count() const noexcept { return threadCount; }

    constexpr usize numa_id() const noexcept { return numaId; }

    constexpr usize numa_thread_count() const noexcept { return numaThreadCount; }

    NumaReplicatedAccessToken numa_access_token() const noexcept { return numaAccessToken; }

    const RootMoves& root_moves() const noexcept { return rootMoves; }

   private:
    bool is_main_worker() const noexcept { return thread_id() == 0; }

    // Get a pointer to the search manager,
    // Only allowed to be called by the main worker.
    MainSearchManager* main_manager() const noexcept {
        assert(is_main_worker());

        return (MainSearchManager*) manager.get();
    }

    void iterative_deepening() noexcept;

    // Main search function for NT nodes
    template<NT T>
    Value search(Position& pos,
                 Stack*    ss,
                 Value     alpha,
                 Value     beta,
                 Depth     depth,
                 i16       red          = 0,
                 Move      excludedMove = Move::None) noexcept;

    // Quiescence search function, which is called by the main search
    template<bool PVNode>
    Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta) noexcept;

    void do_move(Position& pos, Move m, State& st, Stack* ss, bool mayCheck = true) noexcept;
    void undo_move(Position& pos, Move m) noexcept;
    void do_null_move(Position& pos, State& st, Stack* ss) noexcept;
    void undo_null_move(Position& pos) const noexcept;

    Value evaluate(const Position& pos) noexcept;

    void update_capture_history(Piece pc, Square dstSq, PieceType captured, int bonus) noexcept;
    void update_capture_history(const Position& pos, Move m, int bonus) noexcept;

    void update_quiet_history(Color ac, Move m, int bonus) noexcept;

    void update_low_ply_quiet_history(i16 ssPly, Move m, int bonus) noexcept;

    void update_pawn_history(const Position& pos, Piece pc, Square dstSq, int bonus) noexcept;
    void update_pawn_history(const Position& pos, Move m, int bonus) noexcept;

    void update_quiet_histories(const Position& pos, Stack* ss, Move m, int bonus) noexcept;

    template<bool PVNode>
    void update_histories(const Position&             pos,
                          Stack*                      ss,
                          Depth                       depth,
                          Move                        bestMove,
                          bool                        bmTT,
                          const Array<MoveVector, 2>& moveVectors) noexcept;

    void update_correction_histories(const Position& pos, const Stack* ss, int bonus) noexcept;

    int correction_value(const Position& pos, const Stack* ss) const noexcept;

    int history_value(bool                   capture,
                      Move                   m,
                      Piece                  movedPc,
                      PieceType              capturedPt,
                      Color                  ac,
                      const PieceSqHistory** contHistory) const noexcept;
    int history_value(const Position&        pos,
                      Move                   m,
                      Color                  ac,
                      const PieceSqHistory** contHistory) const noexcept;

    bool ponder_move_extracted() noexcept;

    void extend_tb_pv(usize idx, Value& value) noexcept;

    const usize threadId, threadCount, numaId, numaThreadCount;

    const NumaReplicatedAccessToken numaAccessToken;

    ISearchManagerPtr                                  manager;
    const SystemWideLazyNumaReplicated<NNUE::Network>& network;
    const Options&                                     options;
    const TranspositionTable&                          transpositionTable;
    Threads&                                           threads;
    SharedHistories&                                   sharedHistories;

    // Used by NNUE
    NNUE::AccumulatorCache accCache;
    NNUE::AccumulatorStack accStack;

    RelaxedAtomic<u64> nodes, tbHits;
    RelaxedAtomic<u32> moveChanges;

    Position                  rootPos;
    State                     rootState;
    RootMoves                 rootMoves;
    Limit                     limit;
    Tablebase::Syzygy::Config tbConfig;

    Depth rootDepth;
    usize multiPV, pvIdx, pvEnd;
    u16   selDepth;
    u16   rootDelta;
    i16   nmpPly;

    Array<i32, COLOR_NB> optimism;

    PVMoves idxPrePV;

    // Histories
    CaptureHistory captureHistory;

    QuietHistory       quietHistory;
    LowPlyQuietHistory lowPlyQuietHistory;

    ContinuationCorrectionHistory continuationCorrectionHistory;

    TTMoveHistory ttMoveHistory;

    friend class MainSearchManager;
    friend class Position;
    friend class Threads;
};

}  // namespace DON

#endif  // SEARCH_H_INCLUDED

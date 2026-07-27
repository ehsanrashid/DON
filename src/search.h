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
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
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

inline constexpr usize SEARCHED_MOVE_CAPACITY = 32;

using SearchedMoves = FixedVector<Move, SEARCHED_MOVE_CAPACITY, u16>;

inline Book::PolyGlot pgBook;

struct PVMoves final {
   public:
    Move*       begin() noexcept { return moves.data(); }
    const Move* begin() const noexcept { return moves.data(); }

    Move*       end() noexcept { return moves.data() + size(); }
    const Move* end() const noexcept { return moves.data() + size(); }

    Move&       front() noexcept { return moves.front(); }
    const Move& front() const noexcept { return moves.front(); }

    Move&       operator[](usize index) noexcept { return moves[index]; }
    const Move& operator[](usize index) const noexcept { return moves[index]; }

    usize size() const noexcept { return _size; }
    bool  empty() const noexcept { return size() == 0; }

    [[nodiscard]] Move*       data() noexcept { return moves.data(); }
    [[nodiscard]] const Move* data() const noexcept { return moves.data(); }

    void clear() noexcept { _size = 0; }

    void push_back(Move move) noexcept {
        assert(size() < moves.size());
        moves[size()] = move;
        ++_size;
    }

    void resize(usize newSize) noexcept {
        assert(newSize <= size());
        _size = newSize;
    }

    // Appends move and child-Pv[]
    void update(Move move, const PVMoves* childPv) noexcept {
        assert(childPv == nullptr || childPv->size() < moves.size());

        moves[0] = move;
        _size    = 1;

        if (childPv != nullptr)
        {
            auto childPvSize = childPv->size();

            std::memcpy(data() + size(), childPv->data(), childPvSize * sizeof(Move));
            _size += childPvSize;
        }
    }

    // Optimized PV to string conversion (bulk copy)
    std::string build_pv() const noexcept {
        std::string pv;
        pv.reserve(6 * size());

        for (usize i = 0; i < size(); ++i)
        {
            pv.push_back(' ');
            pv.append(move_to_can(moves[i]));
        }

        return pv;
    }

   private:
    std::array<Move, PLY_MAX + 1> moves;
    usize                         _size = 0;
};

// RootMove is used for moves at the root of the tree.
// For each root move store a score and a PV
// (really a refutation in the case of moves which fail low).
// Score is normally set at -VALUE_INFINITE for all non-pv moves.
struct RootMove final {
   public:
    RootMove() noexcept = default;
    explicit RootMove(Move m) noexcept { pv.push_back(m); }

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

    [[nodiscard]] Value effective_value() const noexcept {
        return curValue != -VALUE_INFINITE ? curValue : preValue;
    }

    u64 nodes = 0;

    Value curValue = -VALUE_INFINITE;
    Value preValue = -VALUE_INFINITE;
    Value uciValue = -VALUE_INFINITE;

    Value    avgValue    = -VALUE_INFINITE;
    SqrValue avgSqrValue = sign_sqr(-VALUE_INFINITE);

    i32   tbRank   = 0;
    Value tbValue  = -VALUE_INFINITE;
    u16   selDepth = 0;

    u16 id = UINT16_MAX;

    Bound bound = Bound::NONE;

    PVMoves pv;
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
        rootMoves(rms) {}
    explicit RootMoves(container_type&& rms) noexcept :
        rootMoves(std::move(rms)) {}
    RootMoves(std::initializer_list<value_type> initList) :
        rootMoves(initList) {}

    [[nodiscard]] size_type capacity() const noexcept { return rootMoves.capacity(); }

    [[nodiscard]] bool      empty() const noexcept { return rootMoves.empty(); }
    [[nodiscard]] size_type size() const noexcept { return rootMoves.size(); }

    iterator                     begin() noexcept { return rootMoves.begin(); }
    iterator                     end() noexcept { return rootMoves.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return rootMoves.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return rootMoves.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return rootMoves.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return rootMoves.cend(); }

    [[nodiscard]] reference       front() noexcept { return rootMoves.front(); }
    [[nodiscard]] reference       back() noexcept { return rootMoves.back(); }
    [[nodiscard]] const_reference front() const noexcept { return rootMoves.front(); }
    [[nodiscard]] const_reference back() const noexcept { return rootMoves.back(); }

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

    iterator erase(const_iterator where) noexcept { return rootMoves.erase(where); }
    iterator erase(const_iterator beg, const_iterator end) noexcept {
        return rootMoves.erase(beg, end);
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
        return nodes != 0 ? std::min(constexpr_ceil(double(nodes) / KB) + 1, 512) : 512;
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
    HistoriesMap&                                      historiesMap;
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

    void init() noexcept;

    void check_time(Worker& worker) noexcept override;

    [[nodiscard]] TimePoint elapsed() const noexcept;
    [[nodiscard]] TimePoint elapsed(const Threads& threads) const noexcept;

    void handle_time_management(const Worker& worker,
                                Value         bestValue,
                                Depth         lastCompletedDepth) noexcept;

    void show_pv(Worker& worker, Depth depth) const noexcept;

    void set_ponder(bool pond) noexcept;

    const UpdateContext& updateContext;

    std::mutex              mutex;
    std::condition_variable condVar;

    TimeManager timeManager;
    Skill       skill;
    double      sumMoveChanges;
    double      timeReduction;
    u16         callsCount;
    bool        ponder;
    bool        ponderhitStop;

    Value  preBestCurValue;
    Value  preBestAvgValue;
    double preTimeReduction;
    bool   atFirst;
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

constexpr NT operator~(NT nt) noexcept { return NT((u8(nt) ^ 1) & 1); }

// Stack keeps track of the information need to remember from nodes
// shallower and deeper in the tree during the search.
// Each search thread has its own array of Stack objects, indexed by the ply. (Size = 40)
struct Stack final {
   public:
    PVMoves*                             pv;
    History<HType::PIECE_SQ>*            pieceSqHistory;
    CorrectionHistory<CHType::PIECE_SQ>* pieceSqCorrectionHistory;

    int   history;
    Value evalValue;
    i16   ply;
    Move  move;
    Move  ttMove;
    u16   moveCount;
    u16   cutoffCount;
    bool  inCheck;
    bool  ttPv;
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

    void init() noexcept;

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

    u64 nodes_() const noexcept { return nodes.load(std::memory_order_relaxed); }

   private:
    bool is_main_worker() const noexcept { return thread_id() == 0; }

    // Get a pointer to the search manager,
    // Only allowed to be called by the main worker.
    MainSearchManager* main_manager() const noexcept {
        assert(is_main_worker());

        return (MainSearchManager*) manager.get();
    }

    void iterative_deepening() noexcept;

    // clang-format off

    // Main search function for NT nodes
    template<NT T>
    Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, int red = 0, Move excludedMove = Move::None) noexcept;

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

    void update_quiet_histories(const Position& pos, PawnHistory& pawnHistory, Stack* ss, Move m, int bonus) noexcept;
    void update_histories(const Position& pos, PawnHistory& pawnHistory, Stack* ss, Depth depth, Move bestMove, bool extra, const Array<SearchedMoves, 2>& searchedMoves) noexcept;

    void update_correction_histories(const Position& pos, const Stack* ss, int bonus) noexcept;
    int  correction_value(const Position& pos, const Stack* ss) const noexcept;

    int history_value(bool capture, Move m, Piece movedPc, PieceType capturedPt, Color ac, const History<HType::PIECE_SQ>** contHistory) const noexcept;
    int history_value(const Position& pos, Move m, Color ac, const History<HType::PIECE_SQ>** contHistory) const noexcept;
    // clang-format on

    bool ponder_move_extracted() noexcept;

    void extend_tb_pv(usize index, Value& value) noexcept;

    const usize threadId, threadCount, numaId, numaThreadCount;

    const NumaReplicatedAccessToken numaAccessToken;

    ISearchManagerPtr                                  manager;
    const SystemWideLazyNumaReplicated<NNUE::Network>& network;
    const Options&                                     options;
    const TranspositionTable&                          transpositionTable;
    Threads&                                           threads;
    Histories&                                         histories;
    NNUE::AccumulatorCache                             accCache;
    NNUE::AccumulatorStack                             accStack;

    std::atomic<u64> nodes, tbHits;
    std::atomic<u32> moveChanges;

    Position                  rootPos;
    State                     rootState;
    RootMoves                 rootMoves;
    Limit                     limit;
    Tablebase::Syzygy::Config tbConfig;

    Depth rootDepth, completedDepth;
    usize multiPV, curPV, endPV;
    u16   selDepth;
    u16   rootDelta;
    i16   nmpPly;

    Array<i32, COLOR_NB> optimism;

    // Histories
    History<HType::CAPTURE>   captureHistory;
    History<HType::QUIET>     quietHistory;
    History<HType::LOW_QUIET> lowPlyQuietHistory;
    History<HType::TT_MOVE>   ttMoveHistory;

    Array<History<HType::CONTINUATION>, 2, 2> continuationHistory;  // [inCheck][capture]

    // Correction Histories
    CorrectionHistory<CHType::CONTINUATION> continuationCorrectionHistory;

    friend class MainSearchManager;
    friend class Position;
    friend class Threads;
};

}  // namespace DON

#endif  // #ifndef SEARCH_H_INCLUDED

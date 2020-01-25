#include "Thread.h"

#include <cfloat>
#include <cmath>
#include <map>
#include <iostream>

#include "Option.h"
#include "Searcher.h"
#include "TBsyzygy.h"
#include "Transposition.h"

using namespace std;
using namespace Searcher;
using namespace TBSyzygy;

ThreadPool Threadpool;

namespace {

#if defined(_WIN32)

#   if !defined(NOMINMAX)
#       define NOMINMAX // Disable macros min() and max()
#   endif
#   if !defined(WIN32_LEAN_AND_MEAN)
#       define WIN32_LEAN_AND_MEAN
#   endif
#   if _WIN32_WINNT < 0x0601
#       undef  _WIN32_WINNT
#       define _WIN32_WINNT 0x0601 // Force to include needed API prototypes
#   endif
#   include <windows.h>
#   undef WIN32_LEAN_AND_MEAN
#   undef NOMINMAX

    /// Win Processors Group
    /// Under Windows it is not possible for a process to run on more than one logical processor group.
    /// This usually means to be limited to use max 64 cores.
    /// To overcome this, some special platform specific API should be called to set group affinity for each thread.
    /// Original code from Texel by Peter Osterlund.

    /// The needed Windows API for processor groups could be missed from old Windows versions,
    /// so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    /// try to load them at runtime. To do this first define the corresponding function pointers.
    extern "C"
    {
        typedef bool (*GLPIE) (LOGICAL_PROCESSOR_RELATIONSHIP LogicalProcRelationship, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX PtrSysLogicalProcInfo, PDWORD PtrLength);
        typedef bool (*GNNPME)(USHORT Node, PGROUP_AFFINITY PtrGroupAffinity);
        typedef bool (*STGA)  (HANDLE Thread, CONST GROUP_AFFINITY *GroupAffinity, PGROUP_AFFINITY PtrGroupAffinity);
    }

#endif

    // Skew-logistic function based on naive statistical analysis of
    // "how many games are still undecided after n half-moves".
    // Game is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from the CCRL game database with some simple filtering criteria.
    double move_importance(Depth ply)
    {
        //                                             Shift    Scale   Skew
        return std::max(std::pow(1.00 + std::exp((ply - 64.50) / 6.85), -0.171), DBL_MIN); // Ensure non-zero
    }

    template<bool Optimum>
    TimePoint remaining_time(TimePoint time, u08 movestogo, Depth ply, double move_slowness)
    {
        constexpr auto  StepRatio = 7.30 - 6.30 * Optimum; // When in trouble, can step over reserved time with this ratio
        constexpr auto StealRatio = 0.34 - 0.34 * Optimum; // However must not steal time from remaining moves over this ratio

        auto move_imp1 = move_importance(ply) * move_slowness;
        auto move_imp2 = 0.0;
        for (u08 i = 1; i < movestogo; ++i)
        {
            move_imp2 += move_importance(ply + 2 * i);
        }

        auto time_ratio1 = (1.0) / (1.0 + move_imp2 / (move_imp1 * StepRatio));
        auto time_ratio2 = (1.0 + (move_imp2 * StealRatio) / move_imp1) / (1.0 + move_imp2 / move_imp1);

        return TimePoint(time * std::min(time_ratio1, time_ratio2));
    }
}

/// TimeManager::elapsed_time()
TimePoint TimeManager::elapsed_time() const
{
    return 0 != time_nodes ?
            TimePoint(Threadpool.nodes()) :
            now() - start_time;
}

/// TimeManager::set() calculates the allowed thinking time out of the time control and current game ply.
/// Support four different kind of time controls, passed in 'limit':
///
/// increment == 0, moves to go == 0 => y basetime                             ['sudden death']
/// increment != 0, moves to go == 0 => y basetime + z increment
/// increment == 0, moves to go != 0 => x moves in y basetime                  ['standard']
/// increment != 0, moves to go != 0 => x moves in y basetime + z increment
///
/// Minimum movetime = No matter what, use at least this much time before doing the move, in milli-seconds.
/// Overhead movetime = Attempt to keep at least this much time for each remaining move, in milli-seconds.
/// Move Slowness = Move Slowness, in %age.
void TimeManager::set(Color c, i16 ply)
{
    auto minimum_movetime  = TimePoint(i32(Options["Minimum Move Time"]));
    auto overhead_movetime = TimePoint(i32(Options["Overhead Move Time"]));
    auto move_slowness     = i32(Options["Move Slowness"]) / 100.0;

    time_nodes             = u16(i32(Options["Time Nodes"]));

    // When playing in 'Nodes as Time' mode, then convert from time to nodes, and use values in time management.
    // WARNING: Given NodesTime (nodes per milli-seconds) must be much lower then the real engine speed to avoid time losses.
    if (0 != time_nodes)
    {
        // Only once at after ucinewgame
        if (0 == available_nodes)
        {
            available_nodes = Threadpool.limit.clock[c].time * time_nodes;
        }
        // Convert from milli-seconds to nodes
        Threadpool.limit.clock[c].time = TimePoint(available_nodes);
        Threadpool.limit.clock[c].inc *= time_nodes;
    }

    optimum_time =
    maximum_time = std::max(Threadpool.limit.clock[c].time, minimum_movetime);
    // Plan time management at most this many moves ahead.
    auto max_movestogo = 0 != Threadpool.limit.movestogo ?
                            std::min(Threadpool.limit.movestogo, u08(50)) :
                            u08(50);
    TimePoint time;
    // Calculate optimum time usage for different hypothetic "moves to go" and
    // choose the minimum of calculated search time values.
    for (u08 movestogo = 1; movestogo <= max_movestogo; ++movestogo)
    {
        // Calculate thinking time for hypothetical "moves to go"
        time = std::max(Threadpool.limit.clock[c].time
                      + Threadpool.limit.clock[c].inc * (movestogo - 1)
                        // Clock time: Attempt to keep this much time at clock.
                        // Moves time: Attempt to keep at most this many moves time at clock.
                      - overhead_movetime * (2 + std::min(movestogo, u08(40)))
                      , TimePoint(0));

        optimum_time = std::min(optimum_time, minimum_movetime + remaining_time<true >(time, movestogo, ply, move_slowness));
        maximum_time = std::min(maximum_time, minimum_movetime + remaining_time<false>(time, movestogo, ply, move_slowness));
    }

    if (bool(Options["Ponder"]))
    {
        optimum_time += optimum_time / 4;
    }
}

void TimeManager::update(Color c)
{
    // When playing in 'Nodes as Time' mode
    if (0 != time_nodes)
    {
        available_nodes += Threadpool.limit.clock[c].inc - Threadpool.nodes();
    }
}

PRNG SkillManager::prng{u64(now())}; // PRNG sequence should be non-deterministic.

/// SkillManager::pick_best_move() chooses best move among a set of RootMoves when playing with a strength handicap,
/// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
void SkillManager::pick_best_move()
{
    const auto &root_moves = Threadpool.main_thread()->root_moves;
    assert(!root_moves.empty());
    if (MOVE_NONE == best_move)
    {
        // RootMoves are already sorted by value in descending order
        i32  weakness = DEP_MAX - 4 * level;
        i32  deviance = std::min(root_moves[0].new_value - root_moves[Threadpool.pv_limit - 1].new_value, VALUE_MG_PAWN);
        auto best_value = -VALUE_INFINITE;
        for (u32 i = 0; i < Threadpool.pv_limit; ++i)
        {
            // First for each move score add two terms, both dependent on weakness.
            // One is deterministic with weakness, and one is random with weakness.
            auto value = root_moves[i].new_value
                       + (  weakness * i32(root_moves[0].new_value - root_moves[i].new_value)
                          + deviance * i32(prng.rand<u32>() % weakness)) / DEP_MAX;
            // Then choose the move with the highest value.
            if (best_value <= value)
            {
                best_value = value;
                best_move = root_moves[i].front();
            }
        }
    }
}

/// Thread constructor launches the thread and waits until it goes to sleep in idle_function().
/// Note that 'busy' and 'dead' should be already set.
Thread::Thread(size_t idx)
    : dead(false)
    , busy(true)
    , index(idx)
    , native_thread(&Thread::idle_function, this)
{
    wait_while_busy();
}
/// Thread destructor wakes up the thread in idle_function() and waits for its termination.
/// Thread should be already waiting.
Thread::~Thread()
{
    assert(!busy);
    dead = true;
    start();
    native_thread.join();
}
/// Thread::start() wakes up the thread that will start the search.
void Thread::start()
{
    lock_guard<mutex> guard(mtx);
    busy = true;
    condition_var.notify_one(); // Wake up the thread in idle_function()
}
/// Thread::wait_while_busy() blocks on the condition variable while the thread is busy.
void Thread::wait_while_busy()
{
    unique_lock<mutex> lock(mtx);
    condition_var.wait(lock, [&]{ return !busy; });
}
/// Thread::idle_function() is where the thread is parked.
/// Blocked on the condition variable, when it has no work to do.
void Thread::idle_function()
{
    // If OS already scheduled us on a different group than 0 then don't overwrite
    // the choice, eventually we are one of many one-threaded processes running on
    // some Windows NUMA hardware, for instance in fishtest. To make it simple,
    // just check if running threads are below a threshold, in this case all this
    // NUMA machinery is not needed.
    if (8 < option_threads())
    {
        WinProcGroup::bind(index);
    }

    while (true)
    {
        unique_lock<mutex> lock(mtx);
        busy = false;
        condition_var.notify_one(); // Wake up anyone waiting for search finished
        condition_var.wait(lock, [&]{ return busy; });
        if (dead)
        {
            return;
        }
        lock.unlock();

        search();
    }
}

i16 Thread::move_best_count(Move move) const
{
    return root_moves.move_best_count(pv_cur, pv_end, move);
}

/// Thread::clear() clears all the thread related stuff.
void Thread::clear()
{
    butterfly_history.fill(0);
    capture_history.fill(0);
    for (auto in_check : {0, 1})
    {
        for (auto cap_type : {0, 1})
        {
            for (auto &pdhs : continuation_history[in_check][cap_type])
            {
                for (auto &pdh : pdhs)
                {
                    pdh->fill(0);
                }
            }
            continuation_history[in_check][cap_type][NO_PIECE][0]->fill(CounterMovePruneThreshold - 1);
        }
    }
    for (auto pc : { W_PAWN, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
                     B_PAWN, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING,
                     NO_PIECE })
    {
        for (auto s : SQ)
        {
            move_history[pc][s].fill(MOVE_NONE);
        }
    }

    //// No need to clear
    //pawn_table.clear();
    //matl_table.clear();
}

/// MainThread constructor
MainThread::MainThread(size_t idx)
    : Thread(idx)
{}
/// MainThread::clear()
void MainThread::clear()
{
    Thread::clear();

    check_count = 0;
    best_value = +VALUE_INFINITE;

    time_mgr.time_reduction = 1.00;
    time_mgr.available_nodes = 0;
}

namespace WinProcGroup {

    vector<i16> Groups;

    /// initialize() retrieves logical processor information using specific API
    void initialize()
    {
#   if defined(_WIN32)
        // Early exit if the needed API is not available at runtime
        auto kernel32 = GetModuleHandle("Kernel32.dll");
        if (nullptr == kernel32)
        {
            return;
        }
        // GetLogicalProcessorInformationEx
        auto glpie = GLPIE((void (*)())GetProcAddress(kernel32, "GetLogicalProcessorInformationEx"));
        if (nullptr == glpie)
        {
            return;
        }

        DWORD length;
        // First call to get length. We expect it to fail due to null buffer
        if (glpie(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, nullptr, &length))
        {
            return;
        }

        // Once we know length, allocate the buffer
        auto *ptrSysLogicalProcInfoBase = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(malloc(length));
        if (nullptr == ptrSysLogicalProcInfoBase)
        {
            return;
        }

        // Second call, now we expect to succeed
        if (!glpie(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, ptrSysLogicalProcInfoBase, &length))
        {
            free(ptrSysLogicalProcInfoBase);
            return;
        }

        u16 nodes = 0;
        u16 cores = 0;
        u16 threads = 0;

        DWORD offset = 0;
        auto *ptrSysLogicalProcInfoCurr = ptrSysLogicalProcInfoBase;
        while (offset < length)
        {
            switch (ptrSysLogicalProcInfoCurr->Relationship)
            {
            case LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore:
                ++cores;
                threads += 1 + 1 * (ptrSysLogicalProcInfoCurr->Processor.Flags == LTP_PC_SMT);
                break;
            case LOGICAL_PROCESSOR_RELATIONSHIP::RelationNumaNode:
                ++nodes;
                break;
            default:
                break;
            }
            assert(0 != ptrSysLogicalProcInfoCurr->Size);
            offset += ptrSysLogicalProcInfoCurr->Size;
            ptrSysLogicalProcInfoCurr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)((char*)(ptrSysLogicalProcInfoCurr)+ptrSysLogicalProcInfoCurr->Size);
        }
        free(ptrSysLogicalProcInfoBase);

        // Run as many threads as possible on the same node until core limit is
        // reached, then move on filling the next node.
        for (u16 n = 0; n < nodes; ++n)
        {
            for (u16 i = 0; i < cores / nodes; ++i)
            {
                Groups.push_back(n);
            }
        }

        // In case a core has more than one logical processor (we assume 2) and
        // have still threads to allocate, then spread them evenly across available nodes.
        for (u16 t = 0; t < threads - cores; ++t)
        {
            Groups.push_back(t % nodes);
        }

#   endif
    }
    /// bind() set the group affinity for the thread index.
    void bind(size_t index)
    {
        // If we still have more threads than the total number of logical processors then let the OS to decide what to do.
        if (index >= Groups.size())
        {
            return;
        }

#   if defined(_WIN32)
        u16 group = Groups[index];
        auto kernel32 = GetModuleHandle("Kernel32.dll");
        if (nullptr == kernel32)
        {
            return;
        }
        // GetNumaNodeProcessorMaskEx
        auto gnnpme = GNNPME((void (*)())GetProcAddress(kernel32, "GetNumaNodeProcessorMaskEx"));
        if (nullptr == gnnpme)
        {
            return;
        }
        GROUP_AFFINITY group_affinity;
        if (gnnpme(group, &group_affinity))
        {
            // SetThreadGroupAffinity
            auto stga = STGA((void (*)())GetProcAddress(kernel32, "SetThreadGroupAffinity"));
            if (nullptr == stga)
            {
                return;
            }
            stga(GetCurrentThread(), &group_affinity, nullptr);
        }

#   endif
    }

}

Thread* ThreadPool::best_thread() const
{
    Thread *best_thread = front();

    auto min_value = (*std::min_element(begin(), end(),
                                        [](Thread *const &th1, Thread *const &th2)
                                        {
                                            return th1->root_moves.front().new_value
                                                 < th2->root_moves.front().new_value;
                                        }))->root_moves.front().new_value;

    // Vote according to value and depth
    std::map<Move, u64> votes;
    for (auto *th : *this)
    {
        votes[th->root_moves.front().front()] += i32(th->root_moves.front().new_value - min_value + 14) * th->finished_depth;
    }
    for (auto *th : *this)
    {
        if (best_thread->root_moves.front().new_value >= VALUE_MATE_MAX_PLY)
        {
            // Make sure we pick the shortest mate
            if (best_thread->root_moves.front().new_value < th->root_moves.front().new_value)
            {
                best_thread = th;
            }
        }
        else
        {
            if (   th->root_moves.front().new_value >= VALUE_MATE_MAX_PLY
                || votes[best_thread->root_moves.front().front()] < votes[th->root_moves.front().front()])
            {
                best_thread = th;
            }
        }
    }
    // Select best thread with max depth
    auto best_fm = best_thread->root_moves.front().front();
    for (auto *th : *this)
    {
        if (best_fm == th->root_moves.front().front())
        {
            if (best_thread->finished_depth < th->finished_depth)
            {
                best_thread = th;
            }
        }
    }

    return best_thread;
}

/// ThreadPool::clear() clears the threadpool
void ThreadPool::clear()
{
    for (auto *th : *this)
    {
        th->clear();
    }
}
/// ThreadPool::configure() creates/destroys threads to match the requested number.
/// Created and launched threads will immediately go to sleep in idle_function.
/// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::configure(u32 thread_count)
{
    // Destroy any existing thread(s)
    if (0 < size())
    {
        main_thread()->wait_while_busy();
        while (0 < size())
        {
            delete back();
            pop_back();
        }
    }
    // Create new thread(s)
    if (0 != thread_count)
    {
        push_back(new MainThread(size()));
        while (size() < thread_count)
        {
            push_back(new Thread(size()));
        }

        factor = std::pow(24.8 + std::log(size()) / 2, 2);

        sync_cout << "info string Thread(s) used " << thread_count << sync_endl;

        clear();

        // Reallocate the hash with the new threadpool size
        TT.auto_resize(i32(Options["Hash"]));
    }
}
/// ThreadPool::start_thinking() wakes up main thread waiting in idle_function() and returns immediately.
/// Main thread will wake up other threads and start the search.
void ThreadPool::start_thinking(Position &pos, StateListPtr &states, const Limit &lmt, const vector<Move> &search_moves, bool ponder)
{
    stop = false;
    research = false;
    main_thread()->stop_on_ponderhit = false;
    main_thread()->ponder = ponder;

    limit = lmt;

    RootMoves root_moves;
    root_moves.initialize(pos, search_moves);

    if (!root_moves.empty())
    {
        TBProbeDepth = Depth(i32(Options["SyzygyProbeDepth"]));
        TBLimitPiece = i32(Options["SyzygyLimitPiece"]);
        TBUseRule50 = bool(Options["SyzygyUseRule50"]);
        TBHasRoot = false;

        bool dtz_available = true;

        // Tables with fewer pieces than SyzygyProbeLimit are searched with ProbeDepth == DEPTH_ZERO
        if (TBLimitPiece > MaxLimitPiece)
        {
            TBLimitPiece = MaxLimitPiece;
            TBProbeDepth = DEP_ZERO;
        }

        // Rank moves using DTZ tables
        if (   0 != TBLimitPiece
            && TBLimitPiece >= pos.count()
            && !pos.si->can_castle(CR_ANY))
        {
            // If the current root position is in the table-bases,
            // then RootMoves contains only moves that preserve the draw or the win.
            TBHasRoot = root_probe_dtz(pos, root_moves);
            if (!TBHasRoot)
            {
                // DTZ tables are missing; try to rank moves using WDL tables
                dtz_available = false;
                TBHasRoot = root_probe_wdl(pos, root_moves);
            }
        }

        if (TBHasRoot)
        {
            // Sort moves according to TB rank
            sort(root_moves.begin(), root_moves.end(),
                [](const decltype(root_moves)::value_type &rm1,
                   const decltype(root_moves)::value_type &rm2)
                {
                    return rm1.tb_rank > rm2.tb_rank;
                });

            // Probe during search only if DTZ is not available and we are winning
            if (   dtz_available
                || root_moves.front().tb_value <= VALUE_DRAW)
            {
                TBLimitPiece = 0;
            }
        }
        else
        {
            // Clean up if root_probe_dtz() and root_probe_wdl() have failed
            for (auto &rm : root_moves)
            {
                rm.tb_rank = 0;
            }
        }
    }

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(nullptr != states.get()
        || nullptr != setup_states.get());

    if (nullptr != states.get())
    {
        setup_states = std::move(states); // Ownership transfer, states is now empty
    }

    // We use setup() to set root position across threads.
    // So we need to save and later to restore last stateinfo, cleared by setup().
    // Note that states is shared by threads but is accessed in read-only mode.
    auto fen = pos.fen();
    auto back_si = setup_states->back();
    for (auto *th : *this)
    {
        th->root_depth = DEP_ZERO;
        th->finished_depth = DEP_ZERO;
        th->nodes = 0;
        th->tb_hits = 0;
        th->nmp_ply = 0;
        th->nmp_color = CLR_NO;

        th->root_pos.setup(fen, setup_states->back(), th);
        th->root_moves = root_moves;
    }
    setup_states->back() = back_si;

    main_thread()->start();
}

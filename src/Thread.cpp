#include "Thread.h"

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
        using GLPIE  = bool (*)(LOGICAL_PROCESSOR_RELATIONSHIP LogicalProcRelationship, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX PtrSysLogicalProcInfo, PDWORD PtrLength);
        using GNNPME = bool (*)(USHORT Node, PGROUP_AFFINITY PtrGroupAffinity);
        using STGA   = bool (*)(HANDLE Thread, CONST GROUP_AFFINITY *GroupAffinity, PGROUP_AFFINITY PtrGroupAffinity);
    }

#endif

}



/// Thread constructor launches the thread and waits until it goes to sleep in idleFunction().
/// Note that 'busy' and 'dead' should be already set.
Thread::Thread(size_t idx)
    : dead(false)
    , busy(true)
    , index(idx)
    , nativeThread(&Thread::idleFunction, this)
{
    waitIdle();
}
/// Thread destructor wakes up the thread in idleFunction() and waits for its termination.
/// Thread should be already waiting.
Thread::~Thread()
{
    assert(!busy);
    dead = true;
    start();
    nativeThread.join();
}
/// Thread::start() wakes up the thread that will start the search.
void Thread::start()
{
    lock_guard<mutex> guard(mtx);
    busy = true;
    conditionVar.notify_one(); // Wake up the thread in idleFunction()
}
/// Thread::waitIdle() blocks on the condition variable while the thread is busy.
void Thread::waitIdle()
{
    unique_lock<mutex> lock(mtx);
    conditionVar.wait(lock, [&]{ return !busy; });
}
/// Thread::idleFunction() is where the thread is parked.
/// Blocked on the condition variable, when it has no work to do.
void Thread::idleFunction()
{
    // If OS already scheduled us on a different group than 0 then don't overwrite
    // the choice, eventually we are one of many one-threaded processes running on
    // some Windows NUMA hardware, for instance in fishtest. To make it simple,
    // just check if running threads are below a threshold, in this case all this
    // NUMA machinery is not needed.
    if (8 < optionThreads())
    {
        WinProcGroup::bind(index);
    }

    while (true)
    {
        unique_lock<mutex> lock(mtx);
        busy = false;
        conditionVar.notify_one(); // Wake up anyone waiting for search finished
        conditionVar.wait(lock, [&]{ return busy; });
        if (dead)
        {
            return;
        }
        lock.unlock();

        search();
    }
}

i16 Thread::moveBestCount(Move move) const
{
    return rootMoves.moveBestCount(pvCur, pvEnd, move);
}

/// Thread::clear() clears all the thread related stuff.
void Thread::clear()
{
    quietStats.fill(0);
    captureStats.fill(0);

    for (auto &qcm : quietCounterMoves)
    {
        qcm.fill(MOVE_NONE);
    }

    for (bool inCheck : { false, true })
    {
        for (bool capture : { false, true })
        {
            auto &contStats = continuationStats[inCheck][capture];

            for (auto &c1 : contStats)
            {
                for (auto &c2 : c1)
                {
                    c2.fill(0);
                }
            }

            contStats[NO_PIECE][0].fill(CounterMovePruneThreshold - 1);
        }
    }
}

/// MainThread constructor
MainThread::MainThread(size_t idx)
    : Thread(idx)
{}
/// MainThread::clear()
void MainThread::clear()
{
    Thread::clear();

    tickCount = 0;

    prevBestValue = +VALUE_INFINITE;
    prevTimeReduction = 1.00;

    timeMgr.reset();
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

        u16 nodeCount = 0;
        u16 coreCount = 0;
        u16 threadCount = 0;

        DWORD offset = 0;
        auto *ptrSysLogicalProcInfoCurr = ptrSysLogicalProcInfoBase;
        while (offset < length)
        {
            switch (ptrSysLogicalProcInfoCurr->Relationship)
            {
            case LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore:
                ++coreCount;
                threadCount += 1 + 1 * (ptrSysLogicalProcInfoCurr->Processor.Flags == LTP_PC_SMT);
                break;
            case LOGICAL_PROCESSOR_RELATIONSHIP::RelationNumaNode:
                ++nodeCount;
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
        for (u16 n = 0; n < nodeCount; ++n)
        {
            for (u16 i = 0; i < coreCount / nodeCount; ++i)
            {
                Groups.push_back(n);
            }
        }

        // In case a core has more than one logical processor (we assume 2) and
        // have still threads to allocate, then spread them evenly across available nodes.
        for (u16 t = 0; t < threadCount - coreCount; ++t)
        {
            Groups.push_back(t % nodeCount);
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

Thread* ThreadPool::bestThread() const
{
    Thread *bestThread = front();

    auto minValue = (*std::min_element(begin(), end(),
                                        [](Thread *const &th1, Thread *const &th2)
                                        {
                                            return th1->rootMoves.front().newValue
                                                 < th2->rootMoves.front().newValue;
                                        }))->rootMoves.front().newValue;

    // Vote according to value and depth
    std::map<Move, u64> votes;
    for (auto *th : *this)
    {
        votes[th->rootMoves.front().front()] += i32(th->rootMoves.front().newValue - minValue + 14) * th->finishedDepth;
    }
    for (auto *th : *this)
    {
        if (bestThread->rootMoves.front().newValue >= VALUE_MATE_MAX_PLY)
        {
            // Make sure we pick the shortest mate
            if (bestThread->rootMoves.front().newValue < th->rootMoves.front().newValue)
            {
                bestThread = th;
            }
        }
        else
        {
            if (   th->rootMoves.front().newValue >= VALUE_MATE_MAX_PLY
                || votes[bestThread->rootMoves.front().front()] < votes[th->rootMoves.front().front()])
            {
                bestThread = th;
            }
        }
    }
    // Select best thread with max depth
    auto best_fm = bestThread->rootMoves.front().front();
    for (auto *th : *this)
    {
        if (best_fm == th->rootMoves.front().front())
        {
            if (bestThread->finishedDepth < th->finishedDepth)
            {
                bestThread = th;
            }
        }
    }

    return bestThread;
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
/// Created and launched threads will immediately go to sleep in idleFunction.
/// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::configure(u32 threadCount)
{
    // Destroy any existing thread(s)
    if (0 < size())
    {
        mainThread()->waitIdle();
        while (0 < size())
        {
            delete back();
            pop_back();
        }
    }
    // Create new thread(s)
    if (0 != threadCount)
    {
        push_back(new MainThread(size()));
        while (size() < threadCount)
        {
            push_back(new Thread(size()));
        }

        reductionFactor = std::pow(24.8 + std::log(size()) / 2, 2);

        sync_cout << "info string Thread(s) used " << threadCount << sync_endl;

        clear();

        // Reallocate the hash with the new threadpool size
        TT.autoResize(i32(Options["Hash"]));
    }
}
/// ThreadPool::startThinking() wakes up main thread waiting in idleFunction() and returns immediately.
/// Main thread will wake up other threads and start the search.
void ThreadPool::startThinking(Position &pos, StateListPtr &states, const Limit &lmt, const vector<Move> &searchMoves, bool ponder)
{
    stop = false;
    research = false;
    mainThread()->stopOnPonderhit = false;
    mainThread()->ponder = ponder;

    limit = lmt;

    RootMoves rootMoves;
    rootMoves.initialize(pos, searchMoves);

    if (!rootMoves.empty())
    {
        TBProbeDepth = Depth(i32(Options["SyzygyProbeDepth"]));
        TBLimitPiece = i32(Options["SyzygyLimitPiece"]);
        TBUseRule50 = bool(Options["SyzygyUseRule50"]);
        TBHasRoot = false;

        bool dtzAvailable = true;

        // Tables with fewer pieces than SyzygyProbeLimit are searched with ProbeDepth == DEPTH_ZERO
        if (TBLimitPiece > MaxLimitPiece)
        {
            TBLimitPiece = MaxLimitPiece;
            TBProbeDepth = DEPTH_ZERO;
        }

        // Rank moves using DTZ tables
        if (   0 != TBLimitPiece
            && TBLimitPiece >= pos.count()
            && CR_NONE == pos.castleRights())
        {
            // If the current root position is in the table-bases,
            // then RootMoves contains only moves that preserve the draw or the win.
            TBHasRoot = rootProbeDTZ(pos, rootMoves);
            if (!TBHasRoot)
            {
                // DTZ tables are missing; try to rank moves using WDL tables
                dtzAvailable = false;
                TBHasRoot = rootProbeWDL(pos, rootMoves);
            }
        }

        if (TBHasRoot)
        {
            // Sort moves according to TB rank
            sort(rootMoves.begin(), rootMoves.end(),
                 [](const RootMove &rm1, const RootMove &rm2) { return rm1.tbRank > rm2.tbRank; });

            // Probe during search only if DTZ is not available and winning
            if (   dtzAvailable
                || rootMoves.front().tbValue <= VALUE_DRAW)
            {
                TBLimitPiece = 0;
            }
        }
        else
        {
            // Clean up if rootProbeDTZ() and rootProbeWDL() have failed
            for (auto &rm : rootMoves)
            {
                rm.tbRank = 0;
            }
        }
    }

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(nullptr != states.get()
        || nullptr != setupStates.get());

    if (nullptr != states.get())
    {
        setupStates = std::move(states); // Ownership transfer, states is now empty
    }

    // We use setup() to set root position across threads.
    // So we need to save and later to restore last stateinfo, cleared by setup().
    // Note that states is shared by threads but is accessed in read-only mode.
    auto fen = pos.fen();
    auto back_si = setupStates->back();
    for (auto *th : *this)
    {
        th->rootPos.setup(fen, setupStates->back(), th);
        th->rootMoves       = rootMoves;
        th->rootDepth       = DEPTH_ZERO;
        th->finishedDepth   = DEPTH_ZERO;
        th->nodes           = 0;
        th->tbHits          = 0;
        th->pvChange        = 0;
        th->nmpPly          = 0;
        th->nmpColor        = COLORS;
    }
    setupStates->back() = back_si;

    mainThread()->start();
}

#include "Thread.h"

#include <cmath>
#include <map>
#include <iostream>
#include <type_traits>

#include "Searcher.h"
#include "SyzygyTB.h"
#include "Transposition.h"
#include "UCI.h"

#if defined(_WIN32)

#   if _WIN32_WINNT < 0x0601
#       undef  _WIN32_WINNT
#       define _WIN32_WINNT 0x0601 // Force to include needed API prototypes
#   endif

#   if !defined(NOMINMAX)
#       define NOMINMAX // Disable macros min() and max()
#   endif
#   if !defined(WIN32_LEAN_AND_MEAN)
#       define WIN32_LEAN_AND_MEAN // Excludes APIs such as Cryptography, DDE, RPC, Socket
#   endif

#   include <windows.h>

#   undef WIN32_LEAN_AND_MEAN
#   undef NOMINMAX

    /// The needed Windows API for processor groups could be missed from old Windows versions,
    /// so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    /// try to load them at runtime. To do this first define the corresponding function pointers.
    extern "C" {

        //using GLPIE  = bool (*)(LOGICAL_PROCESSOR_RELATIONSHIP LogicalProcRelationship, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX PtrSysLogicalProcInfo, PDWORD PtrLength);
        using GLPIE  = std::add_pointer<bool(LOGICAL_PROCESSOR_RELATIONSHIP, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD)>::type;
        //using GNNPME = bool (*)(USHORT Node, PGROUP_AFFINITY PtrGroupAffinity);
        using GNNPME = std::add_pointer<bool(USHORT, PGROUP_AFFINITY)>::type;
        //using STGA   = bool (*)(HANDLE Thread, CONST GROUP_AFFINITY *GroupAffinity, PGROUP_AFFINITY PtrGroupAffinity);
        using STGA   = std::add_pointer<bool(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY)>::type;
    }

#endif


ThreadPool Threadpool;

/// Thread constructor launches the thread and waits until it goes to sleep in idleFunction().
/// Note that 'busy' and 'dead' should be already set.
Thread::Thread(u16 index) :
    _index(index),
    _nativeThread(&Thread::idleFunction, this) {
    waitIdle();
}
/// Thread destructor wakes up the thread in idleFunction() and waits for its termination.
/// Thread should be already waiting.
Thread::~Thread() {
    assert(!_busy);
    _dead = true;
    wakeUp();
    _nativeThread.join();
}
/// Thread::wakeUp() wakes up the thread that will start the search.
void Thread::wakeUp() {
    std::lock_guard<std::mutex> lockGuard(_mutex);
    _busy = true;
    _conditionVar.notify_one(); // Wake up the thread in idleFunction()
}
/// Thread::waitIdle() blocks on the condition variable while the thread is busy.
void Thread::waitIdle() {
    std::unique_lock<std::mutex> uniqueLock(_mutex);
    _conditionVar.wait(uniqueLock, [&]{ return !_busy; });
}
/// Thread::idleFunction() is where the thread is parked.
/// Blocked on the condition variable, when it has no work to do.
void Thread::idleFunction() {
    // If OS already scheduled us on a different group than 0 then don't overwrite
    // the choice, eventually we are one of many one-threaded processes running on
    // some Windows NUMA hardware, for instance in fishtest. To make it simple,
    // just check if running threads are below a threshold, in this case all this
    // NUMA machinery is not needed.
    if (8 < optionThreads()) {
        WinProcGroup::bind(_index);
    }

    while (true) {
        {
        std::unique_lock<std::mutex> uniqueLock(_mutex);
        _busy = false;
        _conditionVar.notify_one(); // Wake up anyone waiting for search finished
        _conditionVar.wait(uniqueLock, [&]{ return _busy; });
        } // uniqueLock.unlock();
        if (_dead) {
            return;
        }
        search();
    }
}

/// Thread::clear() clears all the thread related stuff.
void Thread::clear() {
    butterFlyStats.fill(0);
    lowPlyStats.fill(0);

    captureStats.fill(0);

    counterMoves.fill(MOVE_NONE);

    for (bool inCheck : { false, true }) {
        for (bool capture : { false, true }) {
            continuationStats[inCheck][capture].fill(PieceSquareStatsTable{});
            continuationStats[inCheck][capture][NO_PIECE][0].fill(CounterMovePruneThreshold - 1);
        }
    }

    pawnHash.clear();
    matlHash.clear();
}

void MainThread::setTicks(i16 tc) {
    _ticks = tc;
}
/// MainThread::clear()
void MainThread::clear() {
    Thread::clear();

    setTicks(1);

    bestValue = +VALUE_INFINITE;
    timeReduction = 1.00;
}

/// Win Processors Group
/// Under Windows it is not possible for a process to run on more than one logical processor group.
/// This usually means to be limited to use max 64 cores.
/// To overcome this, some special platform specific API should be called to set group affinity for each thread.
/// Original code from Texel by Peter Osterlund.
namespace WinProcGroup {

    std::vector<i16> Groups;

    /// initialize() retrieves logical processor information from specific API
    void initialize() {

#if defined(_WIN32)

        // Early exit if the needed API is not available at runtime
        auto kernel32{ GetModuleHandle("Kernel32.dll") };
        if (kernel32 == nullptr) {
            return;
        }
        auto glpie{ (GLPIE)(void(*)())GetProcAddress(kernel32, "GetLogicalProcessorInformationEx") };
        if (glpie == nullptr) {
            return;
        }

        DWORD buffSize;
        // First call to get size, expect it to fail due to null buffer
        if (glpie(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, nullptr, &buffSize)) {
            return;
        }
        // Once know size, allocate the buffer
        auto *ptrBase{ (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) malloc(buffSize) };
        if (ptrBase == nullptr) {
            return;
        }
        // Second call, now expect to succeed
        if (!glpie(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, ptrBase, &buffSize)) {
            free(ptrBase);
            return;
        }

        u16 nodeCount{ 0 };
        u16 coreCount{ 0 };
        u16 threadCount{ 0 };

        DWORD byteOffset{ 0UL };
        auto *ptrCur{ ptrBase };
        while (byteOffset < buffSize) {
            assert(ptrCur->Size != 0);

            switch (ptrCur->Relationship) {
            case LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore: {
                coreCount += 1;
                threadCount += 1 + 1 * (ptrCur->Processor.Flags == LTP_PC_SMT);
            }
                break;
            case LOGICAL_PROCESSOR_RELATIONSHIP::RelationNumaNode: {
                nodeCount += 1;
            }
                break;
            default:
                break;
            }
            byteOffset += ptrCur->Size;
            ptrCur = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) (((char*)ptrCur) + ptrCur->Size);
        }
        free(ptrBase);

        // Run as many threads as possible on the same node until core limit is
        // reached, then move on filling the next node.
        for (u16 n = 0; n < nodeCount; ++n) {
            for (u16 i = 0; i < coreCount / nodeCount; ++i) {
                Groups.push_back(n);
            }
        }
        // In case a core has more than one logical processor (we assume 2) and
        // have still threads to allocate, then spread them evenly across available nodes.
        for (u16 t = 0; t < threadCount - coreCount; ++t) {
            Groups.push_back(t % nodeCount);
        }

#endif

    }

    /// bind() set the group affinity for the thread index.
    void bind(u16 index) {

        // If we still have more threads than the total number of logical processors then let the OS to decide what to do.
        if (index >= Groups.size()) {
            return;
        }

#if defined(_WIN32)

        u16 group{ u16(Groups[index]) };

        auto kernel32{ GetModuleHandle("Kernel32.dll") };
        if (kernel32 == nullptr) {
            return;
        }

        auto gnnpme{ (GNNPME)(void(*)())GetProcAddress(kernel32, "GetNumaNodeProcessorMaskEx") };
        if (gnnpme == nullptr) {
            return;
        }
        auto stga{ (STGA)(void(*)())GetProcAddress(kernel32, "SetThreadGroupAffinity") };
        if (stga == nullptr) {
            return;
        }

        GROUP_AFFINITY group_affinity;
        if (gnnpme(group, &group_affinity)) {
            stga(GetCurrentThread(), &group_affinity, nullptr);
        }

#endif

    }

}

u16 ThreadPool::size() const {
    return u16(Base::size());
}

MainThread * ThreadPool::mainThread() const {
    return static_cast<MainThread*>(front());
}

Thread* ThreadPool::bestThread() const
{
    Thread *bestTh = front();

    auto minValue = +VALUE_INFINITE;
    for (auto *th : *this) {
        minValue = std::min(th->rootMoves[0].newValue, minValue);
    }
    // Vote according to value and depth
    std::map<Move, u64> votes;
    for (auto *th : *this) {

        votes[th->rootMoves[0][0]] +=
            i32(th->finishedDepth)
          * i32(th->rootMoves[0].newValue - minValue + 14);

        if (bestTh->rootMoves[0].newValue >= +VALUE_MATE_2_MAX_PLY) {
            // Select the shortest mate
            if (bestTh->rootMoves[0].newValue
              <     th->rootMoves[0].newValue) {
                bestTh = th;
            }
        }
        else {
            if (th->rootMoves[0].newValue >= +VALUE_MATE_2_MAX_PLY
             || (votes[bestTh->rootMoves[0][0]]
               < votes[    th->rootMoves[0][0]])) {
                bestTh = th;
            }
        }
    }
    // Select best thread with max depth
    auto bestMove = bestTh->rootMoves[0][0];
    for (auto *th : *this) {
        if (bestMove == th->rootMoves[0][0]) {
            if (bestTh->finishedDepth
              <     th->finishedDepth) {
                bestTh = th;
            }
        }
    }

    return bestTh;
}

/// ThreadPool::setSize() creates/destroys threads to match the requested number.
/// Created and launched threads will immediately go to sleep in idleFunction.
/// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::setup(u16 threadCount) {
    if (!empty()) {
        mainThread()->waitIdle();
    }
    // Destroy any existing thread(s)
    while (!empty()) {
        delete back();
        pop_back();
    }
    // Create new thread(s)
    if (threadCount != 0) {

        push_back(new MainThread(size()));
        while (size() < threadCount) {
            push_back(new Thread(size()));
        }

        clean();

        reductionFactor = std::pow(24.8 + std::log(size()) / 2, 2);
        // Reallocate the hash with the new threadpool size
        TT.autoResize(Options["Hash"]);
    }
}

/// ThreadPool::clean() clears all the threads in threadpool
void ThreadPool::clean() {

    for (auto *th : *this) {
        th->clear();
    }
}

/// ThreadPool::startThinking() wakes up main thread waiting in idleFunction() and returns immediately.
/// Main thread will wake up other threads and start the search.
void ThreadPool::startThinking(Position &pos, StateListPtr &states) {

    stop = false;
    research = false;

    mainThread()->stopOnPonderhit = false;
    mainThread()->ponder = Limits.ponder;

    RootMoves rootMoves;
    rootMoves.initialize(pos, Limits.searchMoves);

    if (!rootMoves.empty()) {
        SyzygyTB::rankRootMoves(pos, rootMoves);
    }

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() != nullptr
        || _states.get() != nullptr);

    if (states.get() != nullptr) {
        _states = std::move(states); // Ownership transfer, states is now empty
    }

    // We use setup() to set root position across threads.
    // So we need to save and later to restore last stateinfo, cleared by setup().
    // Note that states is shared by threads but is accessed in read-only mode.
    auto fen{ pos.fen() };
    auto sBack = _states->back();
    for (auto *th : *this) {
        th->rootDepth       = DEPTH_ZERO;
        th->finishedDepth   = DEPTH_ZERO;
        th->nodes           = 0;
        th->tbHits          = 0;
        th->pvChange        = 0;
        th->nmpPly          = 0;
        th->nmpColor        = COLORS;
        th->lowPlyStats.fill(0);
        th->rootMoves       = rootMoves;
        th->rootPos.setup(fen, _states->back(), th);
    }
    _states->back() = sBack;

    mainThread()->wakeUp();
}

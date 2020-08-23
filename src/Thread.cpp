#include "Thread.h"

#include <cmath>
#include <map>
#include <iostream>
#include <type_traits>

#include "Searcher.h"
#include "SyzygyTB.h"
#include "Transposition.h"
#include "UCI.h"

ThreadPool Threadpool;

/// Thread constructor launches the thread and waits until it goes to sleep in threadFunc().
/// Note that 'busy' and 'dead' should be already set.
Thread::Thread(u16 idx) :
    index(idx),
    nativeThread(&Thread::threadFunc, this) {

    waitIdle();
}
/// Thread destructor wakes up the thread in threadFunc() and waits for its termination.
/// Thread should be already waiting.
Thread::~Thread() {
    assert(!busy);
    dead = true;
    wakeUp();
    nativeThread.join();
}
/// Thread::wakeUp() wakes up the thread that will start the search.
void Thread::wakeUp() {
    std::lock_guard<std::mutex> lockGuard(mutex);
    busy = true;
    conditionVar.notify_one(); // Wake up the thread in threadFunc()
}
/// Thread::waitIdle() blocks on the condition variable while the thread is busy.
void Thread::waitIdle() {
    std::unique_lock<std::mutex> uniqueLock(mutex);
    conditionVar.wait(uniqueLock, [&]{ return !busy; });
}
/// Thread::threadFunc() is where the thread is parked.
/// Blocked on the condition variable, when it has no work to do.
void Thread::threadFunc() {
    // If OS already scheduled us on a different group than 0 then don't overwrite
    // the choice, eventually we are one of many one-threaded processes running on
    // some Windows NUMA hardware, for instance in fishtest. To make it simple,
    // just check if running threads are below a threshold, in this case all this
    // NUMA machinery is not needed.
    if (optionThreads() > 8) {
        WinProcGroup::bind(index);
    }

    while (true) {
        {
        std::unique_lock<std::mutex> uniqueLock(mutex);
        busy = false;
        conditionVar.notify_one(); // Wake up anyone waiting for search finished
        conditionVar.wait(uniqueLock, [&]{ return busy; });
        if (dead) {
            return;
        }
        } //uniqueLock.unlock();
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

    //kingHash.clear();
    //matlHash.clear();
    //pawnHash.clear();
}

/// MainThread::clear()
void MainThread::clear() {
    Thread::clear();

    bestValue = +VALUE_INFINITE;
    timeReduction = 1.00;
}

ThreadPool::~ThreadPool() {
    setup(0);
}

u16 ThreadPool::size() const noexcept {
    return u16(Base::size());
}

MainThread * ThreadPool::mainThread() const noexcept {
    return static_cast<MainThread*>(front());
}

Thread* ThreadPool::bestThread() const noexcept {
    Thread *bestTh{ front() };

    auto minValue{ +VALUE_INFINITE };
    for (auto *th : *this) {
        if (minValue > th->rootMoves[0].newValue) {
            minValue = th->rootMoves[0].newValue;
        }
    }
    // Vote according to value and depth
    std::map<Move, i64> votes;
    for (auto *th : *this) {
        votes[th->rootMoves[0][0]] += i32(th->rootMoves[0].newValue - minValue + 14) * i32(th->finishedDepth);

        if (std::abs(bestTh->rootMoves[0].newValue) < +VALUE_MATE_2_MAX_PLY) {
            if (  th->rootMoves[0].newValue >  -VALUE_MATE_2_MAX_PLY
             && ( th->rootMoves[0].newValue >= +VALUE_MATE_2_MAX_PLY
              ||  votes[bestTh->rootMoves[0][0]] <  votes[th->rootMoves[0][0]]
              || (votes[bestTh->rootMoves[0][0]] == votes[th->rootMoves[0][0]]
               && bestTh->finishedDepth < th->finishedDepth))) {
                bestTh = th;
            }
        }
        else {
            // Select the shortest mate for own/longest mate for opp
            if ( bestTh->rootMoves[0].newValue <  th->rootMoves[0].newValue
             || (bestTh->rootMoves[0].newValue == th->rootMoves[0].newValue
              && bestTh->finishedDepth < th->finishedDepth)) {
                bestTh = th;
            }
        }
    }
    //// Select best thread with max depth
    //auto bestMove{ bestTh->rootMoves[0][0] };
    //for (auto *th : *this) {
    //    if (bestMove == th->rootMoves[0][0]) {
    //        if (bestTh->finishedDepth < th->finishedDepth) {
    //            bestTh = th;
    //        }
    //    }
    //}

    return bestTh;
}

/// ThreadPool::setSize() creates/destroys threads to match the requested number.
/// Created and launched threads will immediately go to sleep in threadFunc.
/// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::setup(u16 threadCount) {
    stop = true;
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
        // Reallocate the hash with the new threadpool size
        u32 hash{ Options["Hash"] };
        TT.autoResize(hash);
        TTEx.autoResize(hash / 4);
        Searcher::initialize();
    }
}

/// ThreadPool::clean() clears all the threads in threadpool
void ThreadPool::clean() {

    for (auto *th : *this) {
        th->clear();
    }
}

/// ThreadPool::startThinking() wakes up main thread waiting in threadFunc() and returns immediately.
/// Main thread will wake up other threads and start the search.
void ThreadPool::startThinking(Position &pos, StateListPtr &states) {

    stop = false;
    research = false;

    mainThread()->stopOnPonderHit = false;
    mainThread()->ponder = Limits.ponder;

    RootMoves rootMoves{ pos, Limits.searchMoves };

    if (!rootMoves.empty()) {
        SyzygyTB::rankRootMoves(pos, rootMoves);
    }

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() != nullptr
        || setupStates.get() != nullptr);

    if (states.get() != nullptr) {
        setupStates = std::move(states); // Ownership transfer, states is now empty
    }

    // We use setup() to set root position across threads.
    // But there are some StateInfo fields (previous, nullPly, captured) that cannot be deduced
    // from a fen string, so setup() clears them and they are set from setupStates->back() later.
    // The rootState is per thread, earlier states are shared since they are read-only.
    auto const fen{ pos.fen() };
    for (auto *th : *this) {
        th->rootDepth     = DEPTH_ZERO;
        th->finishedDepth = DEPTH_ZERO;
        th->nodes         = 0;
        th->tbHits        = 0;
        th->pvChange      = 0;
        th->nmpMinPly     = 0;
        th->nmpColor      = COLORS;
        th->rootMoves     = rootMoves;
        th->rootPos.setup(fen, th->rootState, th);
        th->rootState     = setupStates->back();
    }

    mainThread()->wakeUp();
}

void ThreadPool::wakeUpThreads() {
    for (auto *th : *this) {
        if (th != front()) {
            th->wakeUp();
        }
    }
}
void ThreadPool::waitForThreads() {
    for (auto *th : *this) {
        if (th != front()) {
            th->waitIdle();
        }
    }
}


/// Win Processors Group
/// Under Windows it is not possible for a process to run on more than one logical processor group.
/// This usually means to be limited to use max 64 cores.
/// To overcome this, some special platform specific API should be called to set group affinity for each thread.
/// Original code from Texel by Peter Osterlund.
namespace WinProcGroup {

#if defined(_WIN32)
    #if _WIN32_WINNT < 0x0601
        #undef  _WIN32_WINNT
        #define _WIN32_WINNT 0x0601 // Force to include needed API prototypes
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX // Disable macros min() and max()
    #endif
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN // Excludes APIs such as Cryptography, DDE, RPC, Socket
    #endif

    #include <windows.h>

    #undef NOMINMAX
    #undef WIN32_LEAN_AND_MEAN

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

    namespace {

        /// bestGroup() retrieves logical processor information from specific API
        i16 bestGroup(u16 index) {

            // Early exit if the needed API is not available at runtime
            auto pKernel32{ GetModuleHandle("Kernel32.dll") };
            if (pKernel32 == nullptr) {
                return -1;
            }
            auto pGLPIE{ (GLPIE)(void(*)())GetProcAddress(pKernel32, "GetLogicalProcessorInformationEx") };
            if (pGLPIE == nullptr) {
                return -1;
            }

            DWORD buffSize;
            // First call to get size, expect it to fail due to null buffer
            if (pGLPIE(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, nullptr, &buffSize)) {
                return -1;
            }
            // Once know size, allocate the buffer
            auto *pSLPI{ (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) malloc(buffSize) };
            if (pSLPI == nullptr) {
                return -1;
            }
            // Second call, now expect to succeed
            if (!pGLPIE(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, pSLPI, &buffSize)) {
                free(pSLPI);
                return -1;
            }

            u16 nodeCount{ 0 };
            u16 coreCount{ 0 };
            u16 threadCount{ 0 };

            DWORD byteOffset{ 0UL };
            auto *iSLPI{ pSLPI };
            while (byteOffset < buffSize) {
                assert(iSLPI->Size != 0);

                switch (iSLPI->Relationship) {
                case LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore: {
                    coreCount += 1;
                    threadCount += 1 + 1 * (iSLPI->Processor.Flags == LTP_PC_SMT);
                }
                    break;
                case LOGICAL_PROCESSOR_RELATIONSHIP::RelationNumaNode: {
                    nodeCount += 1;
                }
                    break;
                default:
                    break;
                }
                byteOffset += iSLPI->Size;
                iSLPI = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) (((char*)iSLPI) + iSLPI->Size);
            }
            free(pSLPI);

            std::vector<i16> groups;
            // Run as many threads as possible on the same node until core limit is
            // reached, then move on filling the next node.
            for (u16 n = 0; n < nodeCount; ++n) {
                for (u16 i = 0; i < coreCount / nodeCount; ++i) {
                    groups.push_back(n);
                }
            }
            // In case a core has more than one logical processor (we assume 2) and
            // have still threads to allocate, then spread them evenly across available nodes.
            for (u16 t = 0; t < threadCount - coreCount; ++t) {
                groups.push_back(t % nodeCount);
            }

            // If we still have more threads than the total number of logical processors
            // then return -1 and let the OS to decide what to do.
            return index < groups.size() ? groups[index] : -1;
        }

    }

    /// bind() set the group affinity for the thread index.
    void bind(u16 index) {

        // Use only local variables to be thread-safe
        i16 const group{ bestGroup(index) };
        // If we still have more threads than the total number of logical processors then let the OS to decide what to do.
        if (group == -1) {
            return;
        }

        auto pKernel32{ GetModuleHandle("Kernel32.dll") };
        if (pKernel32 == nullptr) {
            return;
        }

        auto pGNNPME{ (GNNPME)(void(*)())GetProcAddress(pKernel32, "GetNumaNodeProcessorMaskEx") };
        if (pGNNPME == nullptr) {
            return;
        }
        auto pSTGA{ (STGA)(void(*)())GetProcAddress(pKernel32, "SetThreadGroupAffinity") };
        if (pSTGA == nullptr) {
            return;
        }

        GROUP_AFFINITY groupAffinity;
        if (pGNNPME(group, &groupAffinity)) {
            pSTGA(GetCurrentThread(), &groupAffinity, nullptr);
        }
    }
#else
    void bind(u16) {}
#endif

}

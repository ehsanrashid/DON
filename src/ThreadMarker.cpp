#include "ThreadMarker.h"

namespace {

    constexpr u16 ThreadMarkSize{ 0x800 };
    Array<ThreadMark, ThreadMarkSize> ThreadMarks;

}

bool ThreadMark::empty() const {
    return 0 == load(&ThreadMark::posiKey)
        || nullptr == load(&ThreadMark::thread);
}


ThreadMarker::ThreadMarker(Thread const *thread, Key posiKey, i16 ply)
    : threadMark{ nullptr }
    , marked{ false } {

    if (8 > ply) {

        auto *tm{ &ThreadMarks[u16(posiKey) & (ThreadMarkSize - 1)] };
        // Check if another already marked it, if not, mark it
        if (tm->empty()) {
            tm->store(&ThreadMark::thread, thread);
            tm->store(&ThreadMark::posiKey, posiKey);
            threadMark = tm;
        }
        else
        if (tm->load(&ThreadMark::posiKey) == posiKey
         && tm->load(&ThreadMark::thread) != thread) {
            marked = true;
        }
    }
}

ThreadMarker::~ThreadMarker() {
    if (nullptr != threadMark) { // Free the marked location
        threadMark->store(&ThreadMark::thread, static_cast<Thread const*>(nullptr));
        threadMark->store(&ThreadMark::posiKey, U64(0));
        threadMark = nullptr;
    }
}


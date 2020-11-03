#include "threadmarker.h"

namespace {

    constexpr uint16_t ThreadMarkSize{ 0x400 };

    ThreadMark ThreadMarks[ThreadMarkSize];

}


ThreadMarker::ThreadMarker(Thread const *thread, Key posiKey, int16_t ply) noexcept :
    marked{ false },
    owned{ false },
    threadMark{ nullptr } {

    if (ply >= 8) {
        return;
    }

    threadMark = &ThreadMarks[posiKey & (ThreadMarkSize - 1)];
    // Check if another already marked it, if not, mark it
    auto *th{ threadMark->load(&ThreadMark::thread) };
    if (th == nullptr) {
        threadMark->store(&ThreadMark::thread, thread);
        threadMark->store(&ThreadMark::posiKey, posiKey);
        owned = true;
    } else
    if (th != thread
     && threadMark->load(&ThreadMark::posiKey) == posiKey) {
        marked = true;
    }
}

ThreadMarker::~ThreadMarker() noexcept {
    if (owned) { // Free the marked location
        threadMark->store(&ThreadMark::thread, static_cast<Thread const*>(nullptr));
        threadMark->store(&ThreadMark::posiKey, { 0 });
    }
}

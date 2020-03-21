#include "ThreadMarker.h"

namespace {

    constexpr u16 ThreadMarkSize{ 0x800 };

    Array<ThreadMark, ThreadMarkSize> ThreadMarks;

}


ThreadMarker::ThreadMarker(
    Thread const *thread,
    Key posiKey,
    i16 ply) {

    if (ply >= 8) {
        return;
    }

    threadMark = &ThreadMarks[u16(posiKey) & (ThreadMarkSize - 1)];
    // Check if another already marked it, if not, mark it
    auto *th{ threadMark->load(&ThreadMark::thread) };
    auto key{ threadMark->load(&ThreadMark::posiKey) };
    if (th == nullptr) {
        threadMark->store(&ThreadMark::thread, thread);
        threadMark->store(&ThreadMark::posiKey, posiKey);
        owner = true;
    }
    else
    if (th != thread
     && key == posiKey) {
        marked = true;
    }
}

ThreadMarker::~ThreadMarker() {
    if (owner) { // Free the marked location
        threadMark->store(&ThreadMark::thread, static_cast<Thread const*>(nullptr));
        threadMark->store(&ThreadMark::posiKey, u64(0));
    }
}

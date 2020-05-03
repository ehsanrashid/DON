#pragma once

#include <atomic>

#include "Thread.h"
#include "Type.h"

/// ThreadMark are used to mark nodes as being searched by a given thread
struct ThreadMark {

    std::atomic<Thread const*> thread;
    std::atomic<Key>           posiKey;

    template<typename T>
    T load(std::atomic<T> ThreadMark::*member) const {
        return (this->*member).load(std::memory_order::memory_order_relaxed);
    }
    template<typename T>
    void store(std::atomic<T> ThreadMark::*member, T t) {
        (this->*member).store(t, std::memory_order::memory_order_relaxed);
    }

};

/// ThreadMarker structure keeps track of which thread left ThreadMark at the given
/// node for potential reductions. A free node will be marked upon entering the moves
/// loop by the constructor, and unmarked upon leaving that loop by the destructor.
class ThreadMarker {

private:

    ThreadMark *threadMark{ nullptr };
    bool owner{ false };

public:
    bool marked{ false };

    ThreadMarker() = delete;
    ThreadMarker(ThreadMarker const&) = delete;
    ThreadMarker(ThreadMarker&&) = delete;
    ThreadMarker& operator=(ThreadMarker const&) = delete;
    ThreadMarker& operator=(ThreadMarker&&) = delete;

    ThreadMarker(Thread const*, Key, i16);
    ~ThreadMarker();
};


#pragma once

#include <atomic>

#include "thread.h"
#include "type.h"

/// ThreadMark are used to mark nodes as being searched by a given thread
struct ThreadMark {

public:
    template<typename T>
    T load(std::atomic<T> ThreadMark::*member) const noexcept {
        return (this->*member).load(std::memory_order::memory_order_relaxed);
    }
    template<typename T>
    void store(std::atomic<T> ThreadMark::*member, T t) noexcept {
        (this->*member).store(t, std::memory_order::memory_order_relaxed);
    }

    std::atomic<Thread const*> thread;
    std::atomic<Key> posiKey;
};

/// ThreadMarker structure keeps track of which thread left ThreadMark at the given
/// node for potential reductions. A free node will be marked upon entering the moves
/// loop by the constructor, and unmarked upon leaving that loop by the destructor.
class ThreadMarker final {

public:
    ThreadMarker() = delete;
    ThreadMarker(ThreadMarker const&) = delete;
    ThreadMarker(ThreadMarker&&) = delete;
    ThreadMarker& operator=(ThreadMarker const&) = delete;
    ThreadMarker& operator=(ThreadMarker&&) = delete;

    ThreadMarker(Thread const*, Key, i16) noexcept;
    ~ThreadMarker();

    bool marked;

private:

    bool owned;
    ThreadMark *threadMark;
};


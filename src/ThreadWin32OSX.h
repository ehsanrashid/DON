#pragma once

#include <thread>

/// On OSX threads other than the main thread are created with a reduced stack
/// size of 512KB by default, this is too low for deep searches, which require
/// somewhat more than 1MB stack, so adjust it to TH_STACK_SIZE.
/// The implementation calls pthread_create() with the stack size parameter
/// equal to the linux 8MB default, on platforms that support it.
#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)
    #include <pthread.h>

static constexpr size_t TH_STACK_SIZE{ 8 * 1024 * 1024 };

template<typename T, class P = std::pair<T*, void(T::*)()>>
void* startRoutine(void *arg) {
    auto *p{ reinterpret_cast<P*>(arg) };
    (p->first->*(p->second))(); // Call member function pointer
    delete p;
    return NULL;
}

class NativeThread {

private:
    pthread_t thread;

public:

    template<typename T, class P = std::pair<T*, void (T::*)()>>
    explicit NativeThread(void(T::*fun)(), T *obj) {
        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);
        pthread_attr_setstacksize(&thread_attr, TH_STACK_SIZE);

        pthread_create(&thread, &thread_attr, startRoutine<T>, new P(obj, fun));
    }

    void join() {
        pthread_join(thread, NULL);
    }
};
#else // Default case: use STL classes
using NativeThread = std::thread;
#endif

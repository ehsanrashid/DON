#pragma once

#include <thread>

/// On OSX threads other than the main thread are created with a reduced stack
/// size of 512KB by default, this is too low for deep searches, which require
/// somewhat more than 1MB stack, so adjust it to TH_STACK_SIZE.
/// The implementation calls pthread_create() with the stack size parameter
/// equal to the linux 8MB default, on platforms that support it.
#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__)

#include <pthread.h>

class NativeThread
{
private:
    static constexpr size_t TH_STACK_SIZE = 8 * 1024 * 1024;

    template<class T, class P = std::pair<T*, void(T::*)()>>
    static void* start_routine(void *arg)
    {
        P *p = reinterpret_cast<P*>(arg);
        (p->first->*(p->second))(); // Call member function pointer
        delete p;
        return NULL;
    }

    pthread_t thread;

public:

    template<class T, class P = std::pair<T*, void (T::*)()>>
    explicit NativeThread(void(T::*fun)(), T *obj)
    {
        pthread_attr_t attribute, *pattr = &attribute;
        pthread_attr_init(pattr);
        pthread_attr_setstacksize(pattr, TH_STACK_SIZE);
        pthread_create(&thread, pattr, start_routine<T>, new P(obj, fun));
    }
    NativeThread(const NativeThread&) = delete;
    NativeThread& operator=(const NativeThread&) = delete;
    virtual ~NativeThread() {}

    void join() { pthread_join(thread, NULL); }
};

#else // Default case: use STL classes

typedef std::thread NativeThread;

#endif

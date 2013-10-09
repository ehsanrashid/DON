//+ThreadPool
//+==========
//+
//+A simple C++11 Thread Pool implementation

//#pragma once
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <utility>

class ThreadPool;

// our worker thread objects
class Worker
{

private:
    ThreadPool &pool;

public:
    Worker (ThreadPool &p)
        : pool(p)
    {}

    void operator() ();
};

// thread pool without variadics template
class ThreadPool
{

private:
    friend class Worker;

    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;

    // the task queue
    std::queue<std::function<void()> > tasks;

    // synchronization
    std::mutex              queue_mutex;
    std::condition_variable condition;
    bool stop;

protected:

    // ThreadPool is not copyable
    ThreadPool (const ThreadPool &);
    ThreadPool& operator= (const ThreadPool &);

public:

    explicit ThreadPool (size_t threads = std::thread::hardware_concurrency ());

    ~ThreadPool ();

    template<class F>
    auto submit_task (const F &f)
        -> std::future<decltype (std::declval<F>()())>;

    template<class F>
    auto submit_task (F &&f)
        -> std::future<decltype (std::declval<F>()())>;

};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool (size_t threads)
    : stop (false)
{
    for (size_t i = 0; i < threads; ++i)
    {
        workers.emplace_back (std::thread (Worker (*this)));
    }
}

// the destructor joins all threads
ThreadPool::~ThreadPool()
{
    // stop all threads
    {
        std::unique_lock<std::mutex> lock (queue_mutex);
        stop = true;
    }

    condition.notify_all ();

    // join them
    for (size_t i = 0; i < workers.size (); ++i)
    {
        workers[i].join ();
    }
}

// add new work item to the pool
template<class F>
auto ThreadPool::submit_task (const F &f)
    -> std::future<decltype (std::declval<F>()())>
{
    //typedef decltype (f()) return_type;
    typedef decltype (std::declval<F>()()) return_type;

    // don't allow enqueueing after stopping the pool
    if (stop)
    {
        throw std::runtime_error ("submit_task() on stopped thread pool");
    }

    auto task = std::make_shared<std::packaged_task<return_type()> >(f);

    std::future<return_type> fut = task->get_future ();
    {
        // acquire lock
        std::unique_lock<std::mutex> lock (queue_mutex);
        // add the task
        tasks.push ([task] () { (*task)(); });
        // release lock
        lock.unlock ();
    }
    // wake up one thread
    condition.notify_one();
    return fut;
}

// add new work item to the pool
template<class F>
auto ThreadPool::submit_task (F &&f) 
    -> std::future<decltype (std::declval<F>()())>
{
    typedef typename std::result_of<F()>::type return_type;
    
    // don't allow enqueueing after stopping the pool
    if (stop)
    {
        throw std::runtime_error ("submit_task() on stopped thread pool");
    }

    auto task = std::make_shared<std::packaged_task<return_type ()> > (std::forward<F> (f));
    std::future<return_type> fut = task->get_future ();
    {
        // acquire lock
        std::unique_lock<std::mutex> lock (queue_mutex);
        // add the task
        tasks.push ([task] () { (*task)(); });
        // release lock
        lock.unlock ();
    }
    // wake up one thread
    condition.notify_one();
    return fut;
}


//// thread pool using variadics template
//class ThreadPool
//{
//
//private:
//    
//    // need to keep track of threads so we can join them
//    std::vector<std::thread> workers;
//
//    // the task queue
//    std::queue<std::function<void()> > tasks;
//
//    // synchronization
//    std::mutex queue_mutex;
//    std::condition_variable condition;
//    bool stop;
//
//public:
//
//    ThreadPool(size_t);
//
//    ~ThreadPool();
//
//    template<class F, class... Args>
//    auto submit_task (F&& f, Args&&... args) 
//        -> std::future<typename std::result_of<F(Args...)>::type>;
//
//};
//
//// the constructor just launches some amount of workers
//inline ThreadPool::ThreadPool (size_t threads)
//    : stop (false)
//{
//
//    for (size_t i = 0; i < threads; ++i)
//    {
//        workers.emplace_back([this]
//        {
//            while(true)
//            {
//                std::unique_lock<std::mutex> lock(this->queue_mutex);
//                while (!this->stop && this->tasks.empty ())
//                {
//                    this->condition.wait (lock);
//                }
//                if(this->stop && this->tasks.empty ())
//                    return;
//                std::function<void()> task (this->tasks.front ());
//                this->tasks.pop ();
//                lock.unlock ();
//                task ();
//            }
//        });
//    }
//}
//
//// the destructor joins all threads
//inline ThreadPool::~ThreadPool()
//{
//    {
//        std::unique_lock<std::mutex> lock(queue_mutex);
//        stop = true;
//    }
//
//    condition.notify_all();
//    
//    for (size_t i = 0; i < workers.size(); ++i)
//    {
//        workers[i].join ();
//    }
//}
//
//// add new work item to the pool
//template<class F, class... Args>
//auto ThreadPool::submit_task(F&& f, Args&&... args) 
//    -> std::future<typename std::result_of<F(Args...)>::type>
//{
//    typedef typename std::result_of<F(Args...)>::type return_type;
//
//    // don't allow enqueueing after stopping the pool
//    if (stop)
//    {
//        throw std::runtime_error ("submit_task() on stopped thread pool");
//    }
//
//    auto task = std::make_shared<std::packaged_task<return_type()> >(
//        std::bind (std::forward<F> (f), std::forward<Args> (args)...));
//
//    std::future<return_type> fut = task->get_future ();
//    {
//        std::unique_lock<std::mutex> lock (queue_mutex);
//        tasks.push ([task] () { (*task)(); });
//    }
//    
//    condition.notify_one();
//    return fut;
//}
//

inline void Worker::operator() ()
{
    while (true)
    {
        // acquire lock
        std::unique_lock<std::mutex> lock (pool.queue_mutex);

        // look for a work item
        while (!pool.stop && pool.tasks.empty ())
        { // if there are none wait for notification
            pool.condition.wait (lock);
        }
        // exit if the pool is stopped and queue is empty
        if (pool.stop && pool.tasks.empty ()) return;

        // get the task from the queue
        std::function<void()> task (pool.tasks.front ());
        pool.tasks.pop ();

        // release lock
        lock.unlock ();

        // execute the task
        task ();
    }
}

#endif

#pragma once

//#include <deque>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>

namespace std {

#pragma region old

    //template<class T>
    //class blocking_queue
    //{

    //private:

    //    static const int default_timeout = 5000;

    //    std::mutex              _mutex;

    //    std::condition_variable _queue_empty;
    //    std::condition_variable _queue_full;

    //    std::deque<T>           _queue;

    //    const size_t            _capacity;


    //public:

    //    blocking_queue (size_t capacity) 
    //        : _capacity(capacity)
    //        , _mutex ()
    //    {
    //    }

    //    void push (T const &value)
    //    {
    //        {
    //            std::unique_lock<std::mutex> lock (_mutex);
    //            //_queue_full.wait_for (lock, std::chrono::milliseconds(default_timeout), [=] { return !full(); });
    //            _queue_full.wait_for (lock, [=] { return !full(); });
    //            _queue.push_front (value);
    //        }
    //        _queue_empty.notify_one ();
    //    }

    //    T pop ()
    //    {
    //        {
    //            std::unique_lock<std::mutex> lock (_mutex);
    //            _queue_empty.wait_for (lock, std::chrono::milliseconds(default_timeout), [=] { return !empty (); });
    //            //_queue_empty.wait_for (lock, [=] { return !empty (); });

    //            T elem (std::move (_queue.back ()));
    //            _queue.pop_back ();
    //            //T elem (_queue.front ());
    //            //_queue.pop_front ();

    //            _queue_full.notify_one ();
    //            return elem;
    //        }
    //    }

    //    bool empty() const
    //    {
    //        {
    //            std::unique_lock<std::mutex> lock (_mutex);
    //            return _queue.empty ();
    //        }
    //    }

    //    bool full() const
    //    {
    //        {
    //            std::unique_lock<std::mutex> lock (_mutex);
    //            if (_queue.size () > _capacity)
    //            {
    //                throw std::logic_error ("size of blocking_queue cannot be greater than the capacity.");
    //            }
    //            return (_queue.size () == _capacity);
    //        }
    //    }
    //};


#pragma endregion

    template<class T>
    class blocking_queue
        : private std::queue<T> 
    {

    private:

        static const long long default_timeout = 5000;

        mutable std::mutex      _mutex;
        std::atomic<bool>       _shutdown;

        std::condition_variable _queue_empty;
        std::condition_variable _queue_full;

        const size_t            _capacity;

    public:

        blocking_queue (size_t capacity) 
            : std::queue<T> ()
            , _capacity (capacity)
            , _mutex ()
            , _shutdown (false)
        {}

        ~blocking_queue ()
        {}

        void push (const T &value)
        {
            {
                std::unique_lock<std::mutex> lock (_mutex);
                //_queue_full.wait_for (lock, std::chrono::milliseconds(default_timeout), [=] { return !full(); });
                _queue_full.wait (lock, [=] { return !full(); });
                std::queue<T>::push (value);
            }
            _queue_empty.notify_one ();
        }

        T pop ()
        {
            T elem;
            {
                while (empty ())
                {
                    {
                        std::unique_lock<std::mutex> lock (_mutex);
                        //_queue_empty.wait (lock, [=] { return !empty () || _shutdown; });
                        _queue_empty.wait_for (lock, std::chrono::milliseconds (default_timeout), [=] { return !empty () || _shutdown; });
                    }
                    if (_shutdown)
                    {
                        if (empty ()) return T ();
                        break;
                    }
                }
                elem = T (std::move (std::queue<T>::front ()));
                std::queue<T>::pop ();
            }
            _queue_full.notify_one ();
            return elem;
        }

        bool empty () const
        {
            return std::queue<T>::empty ();
        }

        bool full () const
        {
            if (std::queue<T>::size () > _capacity)
            {
                throw std::logic_error ("size of blocking_queue cannot be greater than the capacity.");
            }
            return (std::queue<T>::size () == _capacity);
        }

        inline void shutdown ()
        {
            _shutdown = true;
            _queue_empty.notify_all ();
        }

    };
}


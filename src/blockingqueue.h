#pragma once

#include <mutex>
#include <condition_variable>
#include <deque>
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

    //            T output (std::move (_queue.back ()));
    //            _queue.pop_back ();
    //            //T output (_queue.front ());
    //            //_queue.pop_front ();

    //            _queue_full.notify_one ();
    //            return output;
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
        : private std::deque<T> 
    {

    private:

        static const int default_timeout = 5000;

        mutable std::mutex      _mutex;

        std::condition_variable _queue_empty;
        std::condition_variable _queue_full;

        const size_t            _capacity;


    public:

        blocking_queue (size_t capacity) 
            : std::deque<T> ()
            , _capacity(capacity)
            , _mutex ()
        {
        }

        void push (T const &value)
        {
            {
                std::unique_lock<std::mutex> lock (_mutex);
                //_queue_full.wait_for (lock, std::chrono::milliseconds(default_timeout), [=] { return !full(); });
                _queue_full.wait (lock, [=] { return !full(); });
                std::deque<T>::push_front (value);
            }
            _queue_empty.notify_one ();
        }

        T pop ()
        {
            {
                std::unique_lock<std::mutex> lock (_mutex);
                _queue_empty.wait_for (lock, std::chrono::milliseconds(default_timeout), [=] { return !empty (); });
                //_queue_empty.wait (lock, [=] { return !empty (); });

                T output (std::move (std::deque<T>::back ()));
                std::deque<T>::pop_back ();
                //T output (_queue.front ());
                //std::deque<T>::pop_front ();

                _queue_full.notify_one ();
                return output;
            }
        }

        bool empty() const
        {
            {
                std::lock_guard<std::mutex> lock (_mutex);
                //std::unique_lock<std::mutex> lock (_mutex);
                return std::deque<T>::empty ();
            }
        }

        bool full() const
        {
            {
                std::lock_guard<std::mutex> lock (_mutex);
                //std::unique_lock<std::mutex> lock (_mutex);
                if (std::deque<T>::size () > _capacity)
                {
                    throw std::logic_error ("size of blocking_queue cannot be greater than the capacity.");
                }
                return (std::deque<T>::size () == _capacity);
            }
        }
    };
}



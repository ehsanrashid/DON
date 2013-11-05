//#pragma once
#ifndef TIMER_H_
#define TIMER_H_

#include <thread>
#include <functional>
#include <condition_variable>
#include <future>

#include "Type.h"

namespace std {

    class timer
    {

    private:


    public:

        //template<class F, class... arguments>
        //timer (int32_t interval, bool async, F&& f, arguments&&... args)
        //{
        //    std::function<typename std::result_of<F(arguments...)>::type()> 
        //        task (std::bind (std::forward<F>(f), std::forward<arguments>(args)...));

        //    if (async)
        //    {
        //        std::thread ([interval, task]()
        //        {
        //            std::this_thread::sleep_for (std::chrono::milliseconds (interval));
        //            task ();
        //        }).detach ();
        //    }
        //    else
        //    {
        //        std::this_thread::sleep_for (std::chrono::milliseconds (interval));
        //        task ();
        //    }
        //}

        template<class F>
        timer (int32_t interval, bool async, F &f)
        {
            typedef decltype (std::declval<F>()()) return_type;
            
            std::function<return_type ()> task (f);

            if (async)
            {
                std::thread ([interval, task]()
                {
                    std::this_thread::sleep_for (std::chrono::milliseconds (interval));
                    task ();
                }).detach ();
            }
            else
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (interval));
                task ();
            }
        }


    };


    //template<class F, class... arguments>
    //class timer
    //{

    //private:
    //    int32_t _interval;
    //    bool    _async;

    //    template<class F, class... arguments>
    //    std::function<typename std::result_of<F(arguments...)>::type()> _task;

    //public:

    //    //template<class F, class... arguments>
    //    timer (int32_t interval, bool async, F&& f, arguments&&... args)
    //        : _interval (interval)
    //        , _async (async)
    //        , _task (std::bind (std::forward<F>(f), std::forward<arguments>(args)...))
    //    {
    //        //std::function<typename std::result_of<F(arguments...)>::type()> 
    //        //    task (std::bind (std::forward<F>(f), std::forward<arguments>(args)...));

    //        //if (async)
    //        //{
    //        //    std::thread ([interval, task]()
    //        //    {
    //        //        std::this_thread::sleep_for (std::chrono::milliseconds (interval));
    //        //        task ();
    //        //    }).detach ();
    //        //}
    //        //else
    //        //{
    //        //    std::this_thread::sleep_for (std::chrono::milliseconds (interval));
    //        //    task ();
    //        //}
    //    }


    //    void start ()
    //    {
    //        if (_async)
    //        {
    //            std::thread ([&]()
    //            {
    //                std::this_thread::sleep_for (std::chrono::milliseconds (_interval));
    //                _task ();
    //            }).detach ();
    //        }
    //        else
    //        {
    //            std::this_thread::sleep_for (std::chrono::milliseconds (_interval));
    //            _task ();
    //        }
    //    }

    //    void stop ()
    //    {

    //    }

    //};

}

#endif
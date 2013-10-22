//#pragma once
#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <functional>
#include <chrono>
#include <future>
#include <queue>
#include <thread>
#include <memory>

using std::chrono::system_clock;
using std::chrono::time_point;
using std::chrono::milliseconds;

struct function_timer
{

protected:

    std::function<void()> func;

public:

    system_clock::time_point time;

    function_timer()
    {}

    function_timer(std::function<void()> &&f, system_clock::time_point &t)
        : func (f)
        , time (t)
    {}

    void execute () const
    {
        try
        {
            func ();
        }
        catch (const std::bad_function_call &/*exp*/)
        {
            //TRI_LOG_MSG (exp.what ());
        }
        catch (...) 
        { 
            //TRI_LOG_MSG ("unknown exception"); 
        } 
    }

    // Note: we want our priority_queue to be ordered in terms of smallest time to largest,
    // hence the '>' is used in operator< ().
    // This isn't good practice - it should be a separate struct - but I've done this for brevity.
    // But now I have replaced it with 'friend' function
    friend bool operator< (const function_timer &timer1, const function_timer &timer2)
    { return (timer1.time > timer2.time); }
    friend bool operator> (const function_timer &timer1, const function_timer &timer2)
    { return (timer1.time < timer2.time); }

};

class Scheduler sealed
{

private:

    std::priority_queue<function_timer> tasks;
    std::unique_ptr<std::thread> thread;
    bool go_on;

    Scheduler (const Scheduler &sch);                   // = delete;
    const Scheduler& operator= (const Scheduler &sch);  // = delete;

public:

    Scheduler()
        : go_on (true)
        , thread (new std::thread ([this] () { thread_work (); }))
    { }

    ~Scheduler()
    {
        go_on = false;
        thread->join ();
    }

    void thread_work ()
    {
        while (go_on)
        {
            time_point<system_clock, system_clock::duration> now =
                system_clock::now ();

            while (!tasks.empty () && tasks.top ().time <= now)
            {
                function_timer &f = tasks.top ();
                f.execute ();
                tasks.pop ();
            }

            if (tasks.empty ())
            {
                std::this_thread::sleep_for (milliseconds (100));
            }
            else
            {
                std::this_thread::sleep_for (tasks.top ().time - system_clock::now ());
            }
        }
    }

    void schedule_at (std::function<void()> &&func, system_clock::time_point &time)
    {
        tasks.push (function_timer (std::move (func), time));
    }

    void schedule_every (std::function<void()> func, system_clock::duration interval)
    {
        std::function<void()> wait_func = [this, func, interval] ()
        { 
            func();
            this->schedule_every (func, interval);
        };

        schedule_at (std::move (wait_func), system_clock::now () + interval);
    }

};

#endif // SCHEDULER_H_

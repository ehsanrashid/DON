
#include <iostream>
#include <thread>
#include <cstdlib>
#include <Windows.h>

#include "blockingqueue.h"

//This function will be called from a thread

std::blocking_queue<int> q (50);


//template <typename T, size_t N>
//inline size_t sizeof_array (const T(&)[N])
//{
//    return N;
//}
//
//#include <type_traits>
//template <typename A>
//typename std::enable_if <std::is_array <A> ::value, size_t> ::type sizeof_array (const A &a)
//{
//    return std::extent <A>::value;
//}
//
//const char s[] = "Hello world!";
//
//// sizeof_array (s)


void producer ()
{ 
    for (int i=0; i < 50000; i++)
    {
        // queue.insert blocks if queue is full
        q.push (rand ()+1);    
    }

    // Tell consumer to exit
    //q.push (-1);
    q.shutdown ();            
}

void consumer ()
{ 
    int count = 0;
    while (true)
    {
        int value = q.pop ();
        // queue.get() blocks if queue is empty
        if (value == 0) break;
        ++count;
        std::cout << value << std::endl;
    }
    std::cout << "count: " << count << std::endl;
}

int maine()
{

    //Launch a thread
    //std::thread t1 (call_from_thread);

    //Join the thread with the main thread
    //t1.join();

    //Use of an anonymous function (lambda) in a thread
    //thread t( [] (string name) {
    //    for (int i = 0; i < 100; i++)
    //        cout << "Hello " << name << endl;
    //}, "Tom");

    //////Join the thread with the main thread
    //t.join();
    
    {
    std::thread p (producer);
    Sleep (1000);
    std::thread c (consumer);

    p.join();
    c.join();
    }

    system ("pause");
    return 0;
}

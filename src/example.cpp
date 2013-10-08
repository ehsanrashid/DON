#include <iostream>
#include <vector>
#include <chrono>

#include "atomicstream.h"
#include "ThreadPool.h"

int hey ()
{
    std::atom ()
        << "hello " << "xxx" << std::endl;

    std::this_thread::sleep_for (std::chrono::seconds (1));

    //std::cout
    std::atom ()
        << "world " << "xxx" << std::endl;

    return 100;
}
void hey2 ()
{
    std::atom ()
        << "hello 2 " << "zzz" << std::endl;

}

int main()
{

    ThreadPool pool(2);// (4);

    // queue a bunch of "work items"
    //for (int i = 0; i < 8; ++i)
    //{
    //    pool.submit_task ([i]
    //    {
    //        //std::cout 
    //        std::atom ()
    //            << "hello " << i << std::endl;
    //
    //        std::this_thread::sleep_for (std::chrono::seconds (1));
    //
    //        //std::cout
    //        std::atom ()
    //            << "world " << i << std::endl;
    //        return 0;
    //    });
    //}

    //pool.submit_task (hey);


    std::vector<std::future<int> > futures;
    for (int i = 0; i < 8; ++i)
    {
        futures.emplace_back (pool.submit_task ([i]
        {
            //std::cout
            std::atom ()
                << "hello " << i << std::endl;

            std::this_thread::sleep_for (std::chrono::seconds (1));

            //std::cout
            std::atom ()
                << "world " << i << std::endl;
            return i*i;
        }));
    }   

    std::future<int> fut = pool.submit_task (hey);

    pool.submit_task (hey2);

    for (size_t i = 0; i < futures.size (); ++i)
    {
        //std::cout
        std::atom ()
            << futures[i].get () << std::endl;
    }

    std::atom ()
        << fut.get () << std::endl;

    std::cout << std::endl;
    system ("pause");
    return 0;
}


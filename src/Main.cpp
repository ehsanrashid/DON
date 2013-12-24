#include <iostream>
#include <iomanip>
#include <sstream>

#include "xcstring.h"
#include "xstring.h"
#include "Type.h"
#include "BitBoard.h"
#include "Position.h"
#include "Transposition.h"
#include "Tester.h"
#include "Zobrist.h"
#include "Engine.h"
#include "PolyglotBook.h"
#include "MoveGenerator.h"
#include "iologger.h"
#include "TriLogger.h"
#include "Time.h"
#include "LeakDetector.h"
#include "Evaluator.h"

using namespace std;
using namespace BitBoard;
using namespace MoveGenerator;

namespace {

    void test1()
    {
        cout << "hello";
    }

    void test2(int a)
    {
        printf("%i\n", a);
        return;
    }


    string string_args (size_t argc, const char* const argv[])
    {
        string args;
        for (size_t i = 1; i < argc; ++i)
        {
            args += args.empty () ? string (argv[i]) : " " + string (argv[i]);
        }
        return args;
    }

    void initialize_IO ()
    {
        //size_t size_buf = 0;
        //char *buffer = NULL;
        //setvbuf (stdin, buffer, (buffer) ? _IOFBF : _IONBF, size_buf);
        //setvbuf (stdout, buffer, (buffer) ? _IOFBF : _IONBF, size_buf); // _IOLBF breaks on Windows!

        //cout.unsetf(ios_base::dec);
        //cout.setf (ios_base::boolalpha);
        cout.setf (
            //    ios_base::showpos |
            //    ios_base::boolalpha |
            ios_base::hex |
            //    ios_base::uppercase |
            //    ios_base::fixed |
            //    //ios_base::showpoint |
            ios_base::unitbuf);
        cout.precision (2);
    }

    void print_fill_hex (Key key)
    {
        cout.width (16);
        cout.fill ('0');
        cout << key << endl;
    }

    //char *low_stack, *high_stack;
    //void deepest_stack_path_function()
    //{
    //    int var;
    //    low_stack = (char *) &var;
    //    // ...
    //}
    //void sampling_timer_interrupt_handler()
    //{
    //    int var;
    //    char *cur_stack = (char *) &var;
    //    if (cur_stack < low_stack) low_stack = cur_stack;
    //}

}



int main (int argc, const char* const argv[])
{

    Engine::start ();

    atexit (report_leak);
    return EXIT_SUCCESS;
}





#include "Engine.h"

#include <string>
#include <iostream>
using namespace std;

namespace {

    string strarg (i32 argc, const char *const *argv)
    {
        string arg;
        for (i32 i = 1; i < argc; ++i)
        {
            arg += string (" ", !white_spaces (arg)) + argv[i];
        }
        return arg;
    }

}

i32 main (i32 argc, const char *const *argv)
{
    string arg = strarg (argc, argv);
    Engine::run (arg);

    //atexit (report_leak);
    Engine::exit (EXIT_SUCCESS);
}

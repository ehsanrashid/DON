#include "Engine.h"

using namespace std;

namespace {

    string strarg (i32 argc, const char *const *argv)
    {
        string arg;
        for (i32 i = 1; i < argc; ++i)
        {
            arg += string (" ", !arg.empty ()) + argv[i];
        }
        return arg;
    }

}

i32 main (i32 argc, const char *const *argv)
{
    string arg = strarg (argc, argv);
    Engine::run (arg);

    //system ("pause");
    //atexit (report_leak);
    Engine::exit (EXIT_SUCCESS);
}

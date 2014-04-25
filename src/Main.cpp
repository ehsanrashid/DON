#include "Engine.h"

using namespace std;

namespace {

    string strarg (i32 argc, const char *const *argv)
    {
        string args;
        for (i32 i = 1; i < argc; ++i)
        {
            args += string (" ", !args.empty ()) + argv[i];
        }
        return args;
    }

}

i32 main (i32 argc, const char *const *argv)
{
    string args = strarg (argc, argv);
    Engine::run (args);

    //system ("pause");
    //atexit (report_leak);
    Engine::exit (EXIT_SUCCESS);
}

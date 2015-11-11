#include "Engine.h"

using namespace std;

namespace {

    string strarg (i32 argc, const char *const *argv)
    {
        string arg;
        for (auto i = 1; i < argc; ++i)
        {
            arg += string(argv[i]) + " ";
        }
        return arg;
    }

}

i32 main (i32 argc, const char *const *argv)
{
    string arg = strarg (argc, argv);

    Engine::run (arg);
    Engine::stop (EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

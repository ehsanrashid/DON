#include "Engine.h"

using namespace std;

namespace {

    string strarg (u32 argc, const char *const *argv)
    {
        string arg;
        for (u32 i = 1U; i < argc; ++i)
        {
            arg += string(argv[i]) + " ";
        }
        return arg;
    }

}

i32 main (u32 argc, const char *const *argv)
{
    string arg = strarg (argc, argv);

    Engine::run (arg);
    Engine::stop (EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

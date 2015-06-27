#include "Engine.h"

using namespace std;

namespace {

    string strarg (int argc, const char *const *argv)
    {
        string arg;
        for (auto i = 1; i < argc; ++i)
        {
            arg += string (" ", !white_spaces (arg)) + argv[i];
        }
        return arg;
    }

}

int main (int argc, const char *const *argv)
{
    string arg = strarg (argc, argv);
    Engine::run (arg);

    Engine::exit (EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

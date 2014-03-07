#include "Engine.h"
//#include "LeakDetector.h"

using namespace std;

namespace {

    string strarg (int32_t argc, const char* const argv[])
    {
        string args;
        for (int32_t i = 1; i < argc; ++i)
        {
            args += string (" ", !args.empty ()) + argv[i];
        }
        return args;
    }

}

int32_t main (int32_t argc, const char* const argv[])
{
    string args = strarg (argc, argv);
    Engine::run (args);

    //system ("pause");
    //atexit (report_leak);
    Engine::exit (EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

#include "Engine.h"
//#include "LeakDetector.h"
#include "xstring.h"

using namespace std;

string args_str (size_t argc, const char* const argv[]);

int32_t main (int32_t argc, const char* const argv[])
{
    string args = args_str (argc, argv);
    Engine::start (args);

    //system ("pause");
    //atexit (report_leak);
    return EXIT_SUCCESS;
}

string args_str (size_t argc, const char* const argv[])
{
    string args = "";
    for (size_t i = 1; i < argc; ++i)
    {
        args += whitespace (args) ? string (argv[i]) : " " + string (argv[i]);
    }
    return args;
}

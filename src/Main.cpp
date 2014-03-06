#include "Engine.h"
//#include "LeakDetector.h"
#include "xcstring.h"

using namespace std;

namespace {

    string arg_str (int32_t argc, const char* const argv[])
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
    /*
    uint32_t points[3];
    char **splits;
    uint32_t num_splits;
    //splits = split_str ("AUTH| 150 | 200 ||44|", '|', true, false, &num_splits);
    //splits = split_str ("|||||1|4|||||5", '|', true, false, &num_splits);
    //splits = split_str ("|11|123", '|', true, false, &num_splits);
    splits = split_str ("|1|2|", '|', false, false, &num_splits);

    points[0] = atoi (splits[1]);
    points[1] = atoi (splits[2]);
    points[2] = points[1] - points[0];

    free (*splits);
    free ( splits);
    */

    string args = arg_str (argc, argv);
    Engine::run (args);
    //system ("pause");
    //atexit (report_leak);
    Engine::exit (EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

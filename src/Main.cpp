#include "Engine.h"
//#include "LeakDetector.h"
//#include "xcstring.h"
//#include "xstring.h"

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
    //
    //char ss1[] = "";
    //char ss2[] = "||";
    //string s = "";

    //char **splits;
    //uint32_t num_splits;
    
    //splits = strsplit ("AUTH| 150 | 200 ||44|", '|', true, false, &num_splits);
    //splits = strsplit ("|||||1|4|||||5", '|', true, false, &num_splits);
    //splits = strsplit ("|11|123", '|', true, false, &num_splits);
    //splits = strsplit (ss, '|', false, false, &num_splits);
    //strsplit (s, '|', false);

    //splits = strsplit (ss1, '|', false);
    //free (*splits);
    //free ( splits);

    //splits = strsplit (ss1, '|', true);
    //free (*splits);
    //free ( splits);

    //splits = strsplit (ss2, '|', false);
    //free (*splits);
    //free ( splits);

    //splits = strsplit (ss2, '|', true);
    //free (*splits);
    //free ( splits);

    //return 0;

    string args = arg_str (argc, argv);
    Engine::run (args);

    //system ("pause");
    //atexit (report_leak);
    Engine::exit (EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

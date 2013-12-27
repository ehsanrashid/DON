#include "Engine.h"
//#include "LeakDetector.h"

int32_t main (int32_t argc, const char* const argv[])
{
    Engine::start ();

    system ("pause");
    //atexit (report_leak);
    return EXIT_SUCCESS;
}





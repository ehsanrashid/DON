#include "Engine.h"
//#include "LeakDetector.h"

int main (int argc, const char* const argv[])
{
    Engine::start ();

    system ("pause");
    //atexit (report_leak);
    return EXIT_SUCCESS;
}





#include <cstdlib>

#include "Engine.h"

int main(int argc, const char *const *argv)
{
    // Run the engine
    run(argc, argv);
    // Stop the engine
    stop(EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

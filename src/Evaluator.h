#pragma once

#include <string>

#include "Type.h"

class Position;

namespace Evaluator {

    // The default net name MUST follow the format nn-[SHA256 first 12 digits].nnue
    // for the build process (profile-build and fishtest) to work. Do not change the
    // name of the macro, as it is used in the Makefile.
    #define DefaultEvalFile "nn-03744f8d56d8.nnue"

    extern bool useNNUE;
    extern std::string loadedEvalFile;

    extern void initializeNNUE();
    extern void verifyNNUE();

    namespace NNUE {

        extern bool loadEvalFile(std::istream&);

        extern Value evaluate(Position const&);

    } // namespace NNUE

    extern Value evaluate(Position const&);

    extern std::string trace(Position const&);

}

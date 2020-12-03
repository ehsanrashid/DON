#pragma once

#include <string>

#include "type.h"

class Position;

namespace Evaluator {

    extern bool useNNUE;
    extern std::string loadedEvalFile;

    // The default net name MUST follow the format nn-[SHA256 first 12 digits].nnue
    // for the build process (profile-build and fishtest) to work. Do not change the
    // name of the macro, as it is used in the Makefile.
    #define DefaultEvalFile     "nn-62ef826d1a6d.nnue"

    namespace NNUE {

        extern bool loadEvalFile(std::istream&);

        extern Value evaluate(Position const&);

        extern void initialize() noexcept;

        extern void verify() noexcept;

    }

    extern Value evaluate(Position const&);

    extern std::string trace(Position const&);

}

#pragma once

#include <string>

#include "Type.h"

class Position;

namespace Evaluator {

    extern bool useNNUE;
    extern std::string prevEvalFile;

    extern void initializeNNUE();
    extern void verifyNNUE();

    namespace NNUE {

        extern Value evaluate(Position const&);
        extern Value computeEval(Position const&);
        extern void updateEval(Position const&);
        extern bool loadEvalFile(std::string const&);

    } // namespace NNUE

    extern Value evaluate(Position const&);

    extern std::string trace(Position const&);

}

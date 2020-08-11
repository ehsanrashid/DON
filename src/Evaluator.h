#pragma once

#include <string>

#include "Type.h"

class Position;

namespace Evaluator {

    extern Value evaluate(Position const&);

    extern std::string trace(Position const&);

    extern bool useNNUE;
    extern std::string eval_file_loaded;
    void init_NNUE();
    void verify_NNUE();

    namespace NNUE {

        Value evaluate(const Position& pos);
        Value compute_eval(const Position& pos);
        void  update_eval(const Position& pos);
        bool  load_eval_file(const std::string& evalFile);

    } // namespace NNUE
}

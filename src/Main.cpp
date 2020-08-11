#include <iostream>

#include "Bitbase.h"
#include "Bitboard.h"
#include "Cuckoo.h"
#include "Endgame.h"
#include "Evaluator.h"
#include "Polyglot.h"
#include "PSQTable.h"
#include "Searcher.h"
#include "Thread.h"
#include "TimeManager.h"
#include "Transposition.h"
#include "UCI.h"
#include "Zobrist.h"

int main(int argc, char const *const *argv) {

    std::cout
        << Name << " " << engineInfo() << " by " << Author << "\n"
        << "info string Processor(s) detected " << std::thread::hardware_concurrency() << std::endl;

    BitBoard::initialize();
    BitBase::initialize();
    PSQT::initialize();
    Zobrists::initialize();
    Cuckoos::initialize();
    UCI::initialize(Options);
    EndGame::initialize();
    Book.initialize(Options["Book File"]);
    Threadpool.setup(optionThreads());
    Evaluator::init_NNUE();
    TimeMgr.clear();
    UCI::clear();

    // Join arguments
    std::string cmdLine;
    for (int i = 1; i < argc; ++i) {
        cmdLine += std::string{ argv[i] } + " ";
    }

    UCI::handleCommands(cmdLine);

    TT.free();
    TTEx.free();

    //std::atexit(clear);
    return EXIT_SUCCESS;
}

#include <iostream>

#include "Bitbase.h"
#include "Bitboard.h"
#include "Cuckoo.h"
#include "Endgame.h"
#include "Evaluator.h"
#include "helper.h"
#include "Polyglot.h"
#include "PSQTable.h"
#include "Searcher.h"
#include "Thread.h"
#include "TimeManager.h"
#include "Transposition.h"
#include "UCI.h"
#include "Zobrist.h"

int main(int argc, char const *const argv[]) {

    std::cout << Name << " " << engineInfo() << " by " << Author << '\n';
    std::cout << "info string Processor(s) detected " << std::thread::hardware_concurrency() << '\n';

    CommandLine::initialize(argc, argv);
    UCI::initialize();
    BitBoard::initialize();
    BitBase::initialize();
    PSQT::initialize();
    Zobrists::initialize();
    Cuckoos::initialize();
    EndGame::initialize();
    Book.initialize(Options["Book File"]);
    Threadpool.setup(optionThreads());
    Evaluator::NNUE::initialize();
    UCI::clear();

    UCI::handleCommands(argc, argv);

    Threadpool.setup(0);

    //std::atexit(clear);
    return EXIT_SUCCESS;
}

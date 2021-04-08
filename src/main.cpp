#include <iostream>

#include "bitbase.h"
#include "bitboard.h"
#include "cuckoo.h"
#include "endgame.h"
#include "evaluator.h"
#include "polyglot.h"
#include "psqtable.h"
#include "searcher.h"
#include "thread.h"
#include "timemanager.h"
#include "transposition.h"
#include "uci.h"
#include "zobrist.h"
#include "helper/commandline.h"

int main(int argc, char const *const argv[]) {

    std::cout << Name << " " << engineInfo() << " by " << Author << '\n';
    std::cout << "info string Processor(s) detected " << std::thread::hardware_concurrency() << '\n';

    // path+name of the executable binary, as given by argv[0]
    CommandLine::initialize(argv[0]);
    UCI::initialize();
    //Tune::initialize();
    Bitboards::initialize();
    Bitbases::initialize();
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

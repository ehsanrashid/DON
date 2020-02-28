#include <cstdlib>

#include <iostream>
#include <thread>
#include <cstdlib>

#include "Bitbase.h"
#include "Bitboard.h"
#include "Cuckoo.h"
#include "Endgame.h"
#include "Polyglot.h"
#include "PSQTable.h"
#include "Searcher.h"
#include "Thread.h"
#include "TimeManager.h"
#include "UCI.h"
#include "Zobrist.h"

/// clean() cleans the stuffs in case of some crash.
void clean() {
    Threadpool.stop = true;
    Threadpool.setSize(0);
}

int main(int argc, char const *const *argv) {

    std::cout
        << Name << " " << engineInfo() << " by " << Author << "\n"
        << "info string Processor(s) detected " << std::thread::hardware_concurrency() << std::endl;

    BitBoard::initialize();
    BitBase::initialize();
    PSQT::initialize();
    Zobrists::initialize();
    CucKoo::initialize();
    UCI::initialize();
    Endgames::initialize();
    Book.initialize(Options["Book File"]);
    WinProcGroup::initialize();
    Threadpool.setSize(optionThreads());
    TimeMgr.reset();
    std::srand(u32(time(nullptr)));
    UCI::clear();

    std::atexit(clean);

    // Join arguments
    std::string cmdLine;
    for (int i = 1; i < argc; ++i) {
        cmdLine += std::string{ argv[i] } + " ";
    }

    UCI::handleCommands(cmdLine);

    std::exit(EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

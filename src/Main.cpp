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
#include "UCI.h"
#include "Zobrist.h"

using namespace std;

/// clean() cleans the stuffs in case of some crash.
void clean()
{
    Threadpool.stop = true;
    Threadpool.configure(0);
}

int main(int argc, const char *const *argv)
{
    cout << Name << " " << engineInfo() << " by " << Author << endl;
    cout << "info string Processor(s) detected " << thread::hardware_concurrency() << endl;

    BitBoard::initialize();
    BitBase::initialize();
    PSQT::initialize();
    Zobrists::initialize();
    Cuckooo::initialize();
    UCI::initialize();
    Endgames::initialize();
    Book.initialize(Options["Book File"]);
    WinProcGroup::initialize();
    Threadpool.configure(optionThreads());
    srand(u32(time(nullptr)));
    UCI::clear();

    std::atexit(clean);

    UCI::handleCommands(argc, argv);

    return EXIT_SUCCESS;
}

#include "Benchmark.h"

#include <iostream>
#include <fstream>
#include <vector>

#include "Position.h"
#include "Searcher.h"
#include "Transposition.h"
#include "Thread.h"
#include "UCI.h"
#include "Debugger.h"

using namespace std;
using namespace Searcher;
using namespace Time;
using namespace Threads;

namespace {

    const u08   FEN_TOTAL   = 30;

    const char *DefaultFens[FEN_TOTAL] =
    {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
        "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
        "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
        "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
        "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
        "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
        "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
        "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
        "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
        "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
        "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
        "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 16",
        "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - - 0 11",
        "2K5/p7/7P/5pR1/8/5k2/r7/8 w - - 0 41",
        "8/6pk/1p6/8/PP3p1p/5P2/4KP1q/3Q4 w - - 0 26",
        "7k/3p2pp/4q3/8/4Q3/5Kp1/P6b/8 w - - 0 39",
        "8/2p5/8/2kPKp1p/2p4P/2P5/3P4/8 w - - 0 35",
        "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 33",
        "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - - 0 21",
        "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - - 0 17",
        "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - - 0 40",
        "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - - 0 29",
        "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - - 0 15",
        "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - - 0 19",
        "8/3p3B/5p2/5P2/p7/PP5b/k7/6K1 w - - 0 30"
    };

}

// benchmark() runs a simple benchmark by letting engine analyze a set of positions for a given limit each.
// There are five optional parameters:
//  - transposition table size (default is 32 MB).
//  - number of search threads that should be used (default is 1 thread).
//  - limit value spent for each position (default is 13 depth),
//  - type of the limit value:
//     * 'depth' (default).
//     * 'time' in secs
//     * 'nodes' to search.
//     * 'mate' in moves
//  - filename where to look for positions in fen format (defaults are the positions defined above)
//     * 'default' for builtin position
//     * 'current' for current position
//     * '<filename>' containing fens position
// example: bench 32 1 10 depth default
void benchmark (istream &is, const Position &pos)
{
    string token;
    vector<string> fens;

    // Assign default values to missing arguments
    string hash       = (is >> token) ? token : "32";
    string threads    = (is >> token) ? token : "1";
    string limit_val  = (is >> token) ? token : "13";
    string limit_type = (is >> token) ? token : "depth";
    string fen_fn     = (is >> token) ? token : "default";

    Options["Hash"]    = hash;
    Options["Threads"] = threads;

    i32 value = abs (atoi (limit_val.c_str ()));
    //value = value >= 0 ? +value : -value;

    LimitsT limits;
    if      (limit_type == "time")  limits.movetime = value * M_SEC; // movetime is in ms
    else if (limit_type == "nodes") limits.nodes    = value;
    else if (limit_type == "mate")  limits.mate     = value;
    //else if (limit_type == "depth")
    else                            limits.depth    = value;

    if      (fen_fn == "default")
    {
        fens.assign (DefaultFens, DefaultFens + FEN_TOTAL);
    }
    else if (fen_fn == "current")
    {
        fens.push_back (pos.fen ());
    }
    else
    {
        ifstream ifs (fen_fn.c_str ());

        if (!ifs.is_open ())
        {
            cerr << "ERROR: Unable to open file ... \'" << fen_fn << "\'" << endl;
            return;
        }

        string fen;
        while (getline (ifs, fen))
        {
            if (!fen.empty ())
            {
                fens.push_back (fen);
            }
        }

        ifs.close ();
    }
    
    bool  chess960 = bool (Options["UCI_Chess960"]);
    u64   nodes    = 0;
    point elapsed  = now ();

    StateInfoStackPtr states;

    u16 total = fens.size ();
    for (u16 i = 0; i < total; ++i)
    {
        Position root_pos (fens[i], Threadpool.main (), chess960);

        cerr
            << "\n--------------\n" 
            << "Position: " << (i + 1) << "/" << total << "\n";

        if (limit_type == "perft")
        {
            u64 leaf_count = perft (root_pos, i32 (limits.depth) * ONE_MOVE);
            cerr << "\nPerft " << u16 (limits.depth)  << " leaf nodes: " << leaf_count << "\n";
            nodes += leaf_count;
        }
        else
        {
            TT.master_clear ();
            Threadpool.start_thinking (root_pos, limits, states);
            Threadpool.wait_for_think_finished ();
            nodes += RootPos.game_nodes ();
        }
    }

    cerr<< "\n---------------------------\n";

    Debugger::dbg_print (); // Just before to exit

    elapsed = now () - elapsed;
    // Ensure non-zero to avoid a 'divide by zero'
    if (elapsed == 0) elapsed = 1;

    cerr
        << "\n===========================\n"
        << "Total time (ms) : " << elapsed << "\n"
        << "Nodes searched  : " << nodes   << "\n"
        << "Nodes/second    : " << nodes * 1000 / elapsed
        << "\n---------------------------\n" << endl;
}
/*
void benchtest (istream &is, const Position &pos)
{
    string token;
    vector<string> fens;

    // Assign default values to missing arguments
    string hash       = (is >> token) ? token : "1024";
    string threads    = (is >> token) ? token : "4";
    string limit_val  = (is >> token) ? token : "15";
    string limit_type = (is >> token) ? token : "depth";
    string fen_fn     = (is >> token) ? token : "default";

    Options["Hash"]    = hash;
    Options["Threads"] = threads;

    i32 value = abs (atoi (limit_val.c_str ()));
    //value = value >= 0 ? +value : -value;

    LimitsT limits;
    if      (limit_type == "time")  limits.movetime = value * M_SEC; // movetime is in ms
    else if (limit_type == "nodes") limits.nodes    = value;
    else if (limit_type == "mate")  limits.mate     = value;
    //else if (limit_type == "depth")
    else                            limits.depth    = value;

    if      (fen_fn == "default")
    {
        fens.assign (DefaultFens, DefaultFens + FEN_TOTAL);
    }
    else if (fen_fn == "current")
    {
        fens.push_back (pos.fen ());
    }
    else
    {
        ifstream ifs (fen_fn.c_str ());

        if (!ifs.is_open ())
        {
            cerr << "ERROR: Unable to open file ... \'" << fen_fn << "\'" << endl;
            return;
        }

        string fen;
        while (getline (ifs, fen))
        {
            if (!fen.empty ())
            {
                fens.push_back (fen);
            }
        }

        ifs.close ();
    }
    
    bool chess960  = bool (Options["UCI_Chess960"]);
    u64 nodes      = 0;
    point elapsed  = now ();

    StateInfoStackPtr states;

    u16 total = fens.size ();
    for (u16 i = 0; i < total; ++i)
    {
        Position root_pos (fens[i], Threadpool.main (), chess960);

        cerr
            << "\n--------------\n" 
            << "Position: " << (i + 1) << "/" << total << "\n";

        if (limit_type == "perft")
        {
            u64 leaf_count = perft (root_pos, i32 (limits.depth) * ONE_MOVE);
            cerr << "\nPerft " << u16 (limits.depth)  << " leaf nodes: " << leaf_count << "\n";
            nodes += leaf_count;
        }
        else
        {
            TT.master_clear ();
            Threadpool.start_thinking (root_pos, limits, states);
            Threadpool.wait_for_think_finished ();
            nodes += RootPos.game_nodes ();
        }
    }

    cerr<< "\n---------------------------\n";

    Debugger::dbg_print (); // Just before to exit

    elapsed = now () - elapsed;
    // Ensure non-zero to avoid a 'divide by zero'
    if (elapsed == 0) elapsed = 1;

    cerr
        << "\n===========================\n"
        << "Total time (ms) : " << elapsed << "\n"
        << "Nodes searched  : " << nodes   << "\n"
        << "Nodes/second    : " << nodes * 1000 / elapsed
        << "\n---------------------------\n" << endl;
}
*/

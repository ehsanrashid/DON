#include "Benchmark.h"

#include <iostream>
#include <fstream>
#include <vector>

#include "UCI.h"
#include "Position.h"
#include "Searcher.h"
#include "Transposition.h"
#include "Thread.h"
#include "Notation.h"
#include "Debugger.h"

using namespace std;
using namespace Searcher;
using namespace MoveGenerator;
using namespace Time;
using namespace Threads;
using namespace Notation;

namespace {

    const u08   FEN_TOTAL   = 30;

    const char *DefaultFens[FEN_TOTAL] =
    {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - -",
        "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - -",
        "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - -",
        "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - -",
        "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq -",
        "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - -",
        "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - -",
        "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ -",
        "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - -",
        "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - -",
        "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - -",
        "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - -",
        "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - -",
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - -",
        "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - -",
        "2K5/p7/7P/5pR1/8/5k2/r7/8 w - -",
        "8/6pk/1p6/8/PP3p1p/5P2/4KP1q/3Q4 w - -",
        "7k/3p2pp/4q3/8/4Q3/5Kp1/P6b/8 w - -",
        "8/2p5/8/2kPKp1p/2p4P/2P5/3P4/8 w - -",
        "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - -",
        "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - -",
        "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - -",
        "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - -",
        "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - -",
        "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - -",
        "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - -",
        "8/3p3B/5p2/5P2/p7/PP5b/k7/6K1 w - -"
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
    string boolean    = "false";

    Options["Hash"]    = hash;
    Options["Never Clear Hash"] = boolean;
    Options["Threads"] = threads;

    i32 value = abs (atoi (limit_val.c_str ()));
    //value = value >= 0 ? +value : -value;

    LimitsT limits;
    if      (limit_type == "time")  limits.movetime = value * M_SEC; // movetime is in ms
    else if (limit_type == "nodes") limits.nodes    = value;
    else if (limit_type == "mate")  limits.mate     = u08 (value);
    //else if (limit_type == "depth")
    else                            limits.depth    = u08 (value);

    if (fen_fn == "default")
    {
        fens.assign (DefaultFens, DefaultFens + FEN_TOTAL);
    }
    else
    if (fen_fn == "current")
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
    point time     = now ();

    StateInfoStackPtr states;

    u16 total = fens.size ();
    for (u16 i = 0; i < total; ++i)
    {
        Position root_pos (fens[i], Threadpool.main (), chess960, false);

        cerr
            << "\n---------------\n" 
            << "Position: " << setw (2) << (i + 1) << "/" << total << "\n";

        if (limit_type == "perft")
        {
            u64 leaf_count = perft (root_pos, i32 (limits.depth) * ONE_MOVE);
            cerr << "\nDepth " << u16 (limits.depth)  << " leaf nodes: " << leaf_count << "\n";
            nodes += leaf_count;
        }
        else
        if (limit_type == "perftdiv")
        {
            StateInfo si;
            CheckInfo ci (root_pos);
            cerr << "\nDepth " << u16 (limits.depth) << "\n";
            for (MoveList<LEGAL> ms (root_pos); *ms != MOVE_NONE; ++ms)
            {
                root_pos.do_move (*ms, si, root_pos.gives_check (*ms, ci) ? &ci : NULL);
                u64 leaf_count = limits.depth > 1 ? perft (root_pos, (limits.depth - 1)*ONE_MOVE) : 1;
                root_pos.undo_move ();
                cerr << move_to_can (*ms, root_pos.chess960 ()) << ": " << leaf_count << "\n";
                nodes += leaf_count;
            }
        }
        else
        {
            TT.clear ();
            Threadpool.start_thinking (root_pos, limits, states);
            Threadpool.wait_for_think_finished ();
            nodes += RootPos.game_nodes ();
        }
    }

    cerr<< "\n---------------------------\n";

    Debugger::dbg_print (); // Just before to exit

    time = now () - time;
    // Ensure non-zero to avoid a 'divide by zero'
    if (time == 0) time = 1;

    cerr
        << "\n===========================\n"
        << "Total time (ms) : " << time << "\n"
        << "Nodes searched  : " << nodes   << "\n"
        << "Nodes/second    : " << nodes * M_SEC / time
        << "\n---------------------------\n" << endl;
}

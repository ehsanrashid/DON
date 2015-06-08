#include "Benchmark.h"

#include "Position.h"
#include "Searcher.h"
#include "UCI.h"
#include "Transposition.h"
#include "Thread.h"
#include "Notation.h"
#include "Debugger.h"

using namespace std;
using namespace Searcher;
using namespace Transposition;
using namespace MoveGen;
using namespace Notation;

namespace {

    const vector<string> DEFAULT_FEN =
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
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1",
        "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - - 0 1",
        "2K5/p7/7P/5pR1/8/5k2/r7/8 w - - 0 1",
        "8/6pk/1p6/8/PP3p1p/5P2/4KP1q/3Q4 w - - 0 1",
        "7k/3p2pp/4q3/8/4Q3/5Kp1/P6b/8 w - - 0 1",
        "8/2p5/8/2kPKp1p/2p4P/2P5/3P4/8 w - - 0 1",
        "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1",
        "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - - 0 1",
        "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - - 0 1",
        "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - - 0 1",
        "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - - 0 1",
        "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - - 0 1",
        "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - - 0 1",
        "8/3p3B/5p2/5P2/p7/PP5b/k7/6K1 w - - 0 1",
        
        // 5-man positions
        "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",     // Kc2 - Mate
        "8/8/8/5N2/8/p7/8/2NK3k w - - 0 1",      // Na2 - Mate
        "8/3k4/8/8/8/4B3/4KB2/2B5 w - - 0 1",    // Draw
        
        // 6-man positions
        "8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 1",   // Re5 - Mate
        "8/2p4P/8/kr6/6R1/8/8/1K6 w - - 0 1",    // Ka2 - Mate
        "8/8/3P3k/8/1p6/8/1P6/1K3n2 b - - 0 1",  // Nd2 - Draw
        
        // 7-man positions
        "8/R7/2q5/8/6k1/8/1P5p/K6R w - - 0 124", // Draw
    };
}

// benchmark() runs a simple benchmark by letting engine analyze a set of positions for a given limit each.
// There are five optional parameters:
//  - Transposition table size (default is 16 MB)
//  - Number of search threads that should be used (default is 1 Thread)
//  - Limit value spent for each position (default is 13 Depth)
//  - Type of the Limit value:
//     * 'depth' (default)
//     * 'time' [millisecs]
//     * 'movetime' [millisecs]
//     * 'nodes'
//     * 'mate'
//  - FEN positions to be used:
//     * 'default' for builtin positions (default)
//     * 'current' for current position
//     * '<filename>' for file containing FEN positions
// example: bench 32 1 10000 movetime default
void benchmark (istream &is, const Position &cur_pos)
{
    string token;
    // Assign default values to missing arguments
    string hash       = (is >> token) && !white_spaces (token) ? token : to_string (TranspositionTable::DefSize);
    string threads    = (is >> token) && !white_spaces (token) ? token : "1";
    string limit_val  = (is >> token) && !white_spaces (token) ? token : "13";
    string limit_type = (is >> token) && !white_spaces (token) ? token : "depth";
    string fen_fn     = (is >> token) && !white_spaces (token) ? token : "default";
    string never_clear_hash = "false";
    
    Options["Hash"]             = hash;
    Options["Threads"]          = threads;
    Options["Never Clear Hash"] = never_clear_hash;
    
    i32 value = abs (stoi (limit_val));

    LimitsT limits;
    if      (limit_type == "time")     limits.clock[WHITE].time = limits.clock[BLACK].time = value;
    else if (limit_type == "movetime") limits.movetime = value;
    else if (limit_type == "nodes")    limits.nodes    = value;
    else if (limit_type == "mate")     limits.mate     = u08(value);
    else  /*(limit_type == "depth")*/  limits.depth    = u08(value);

    vector<string> fens;
    
    if (fen_fn == "default")
    {
        fens = DEFAULT_FEN;
    }
    else
    if (fen_fn == "current")
    {
        fens.push_back (cur_pos.fen ());
    }
    else
    {
        ifstream fen_ifs (fen_fn);

        if (!fen_ifs.is_open ())
        {
            cerr << "ERROR: unable to open file ... \'" << fen_fn << "\'" << endl;
            return;
        }

        string fen;
        while (!fen_ifs.eof () && getline (fen_ifs, fen))
        {
            if (!white_spaces (fen))
            {
                fens.push_back (fen);
            }
        }

        fen_ifs.close ();
    }

    StateInfoStackPtr states;
    reset ();

    u64       nodes = 0;
    TimePoint time  = now ();
    
    for (u16 i = 0; i < fens.size (); ++i)
    {
        Position pos (fens[i], Threadpool.main (), Chess960, false);

        cerr << "\n---------------\n" 
             << "Position: " << setw (2) << (i + 1) << "/" << fens.size () << "\n";

        if (limit_type == "perft")
        {
            cerr << "\nDepth " << i32(limits.depth) << "\n";
            u64 leaf_nodes = perft (pos, Depth(limits.depth));
            cout << "\nLeaf nodes: " << leaf_nodes << "\n";
            nodes += leaf_nodes;
        }
        else
        {
            Threadpool.start_main (pos, limits, states);
            Threadpool.main ()->join ();
            nodes += RootPos.game_nodes ();
        }
    }

    time = max (now () - time, TimePoint (1));
    
    cerr << "\n---------------------------\n";
    Debugger::dbg_print (); // Just before to exit
    cerr << "\n===========================\n"
         << "Total time (ms) : " << time  << "\n"
         << "Nodes searched  : " << nodes << "\n"
         << "Nodes/second    : " << nodes * MILLI_SEC / time
         << "\n---------------------------\n" << endl;
}

void auto_tune (istream &is)
{
    string token;
    string threads = (is >> token) && !white_spaces (token) ? token : "1";

    Options["Threads"] = threads;
    
    LimitsT limits;
    limits.depth = 15;

    vector<string> fens = DEFAULT_FEN;
    
    u64 nps[4];
    for (i32 d = 0; d < 4; ++d)
    {
        Threadpool.split_depth = (d+4)*DEPTH_ONE;
        cerr << "Split Depth     : " << i32(Threadpool.split_depth);
        
        StateInfoStackPtr states;
        reset ();

        u64       nodes = 0;
        TimePoint time  = now ();

        for (u16 i = 0; i < fens.size (); ++i)
        {
            Position root_pos (fens[i], Threadpool.main (), Chess960, false);

            cerr << "\n---------------\n" 
                 << "Position: " << setw (2) << (i + 1) << "/" << fens.size () << "\n";

            Threadpool.start_main (root_pos, limits, states);
            Threadpool.main ()->join ();
            nodes += RootPos.game_nodes ();
        }

        time = max (now () - time, TimePoint (1));

        nps[d] = nodes* MILLI_SEC/time;
    }

    Depth opt_split_depth = DEPTH_ZERO;
    u64   max_nps         = 0;
    for (i32 d = 0; d < 4; ++d)
    {
        cerr << "\n---------------------------\n"
             << "Split Depth  : " << d+4  << "\n"
             << "Nodes/second : " << nps[d]
             << "\n---------------------------" << endl;

        if (max_nps < nps[d])
        {
            max_nps = nps[d];
            opt_split_depth = (d+4)*DEPTH_ONE;
        }
    }

    Threadpool.split_depth = opt_split_depth;
    sync_cout << "info string Split Depth " << u16(opt_split_depth) << sync_endl;
}

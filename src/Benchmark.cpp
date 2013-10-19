#include <fstream>
#include <iostream>
#include <vector>
#include "xstring.h"
#include "Position.h"
#include "Searcher.h"
#include "Transposition.h"
#include "UCI.h"
#include "TriLogger.h"

namespace {

    const uint16_t NUM_FEN = 16;

    const char* default_fens[NUM_FEN] =
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
        "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26"
    };

}

// benchmark() runs a simple benchmark by letting engine analyze a set of positions for a given limit each.
// There are five optional parameters:
//  - transposition table size (default is 32 MB).
//  - number of search threads that should be used (default is 1 thread).
//  - filename where to look for positions in fen format (defaults are the positions defined above)
//  - limit value spent for each position (default is 12 depth),
//  - type of the limit value:
//     * depth (default).
//     * time in secs
//     * number of nodes.
void benchmark (std::istream &is, const Position &pos)
{
    std::string token;
    std::vector<std::string> fens;

    try
    {
        // Assign default values to missing arguments
        std::string size_tt    = (is >> token) ? token : "32";
        std::string threads    = (is >> token) ? token : "1";
        std::string fn_fen     = (is >> token) ? token : "default";
        std::string limit_val  = (is >> token) ? token : "12";
        std::string limit_type = (is >> token) ? token : "depth";

        //Options["Hash"]    = size_tt;
        //Options["Threads"] = threads;
        TT.clear();
        Searcher::Limits limits;

        if (false);
        else if (iequals (limit_type, "time"))  limits.move_time = std::stoi (limit_val) * 1000; // movetime is in ms
        else if (iequals (limit_type, "nodes")) limits.nodes     = std::stoi (limit_val);
        else if (iequals (limit_type, "mate"))  limits.mate_in   = std::stoi (limit_val);
        //if (iequals (limit_type, "depth"))
        else                                    limits.depth     = std::stoi (limit_val);

        if (false);
        else if (iequals (fn_fen, "default"))
        {
            fens.assign (default_fens, default_fens + NUM_FEN);
        }
        else if (iequals (fn_fen, "current"))
        {
            fens.emplace_back (pos.fen ());
        }
        else
        {
            std::string fen;
            std::ifstream ifstm_fen (fn_fen);

            if (!ifstm_fen.is_open ())
            {
                TRI_LOG_MSG ("ERROR: Unable to open file ... \'" << fn_fen << "\'");
                return;
            }

            while (std::getline (ifstm_fen, fen))
            {
                if (!fen.empty ())
                {
                    fens.emplace_back (fen);
                }
            }
            ifstm_fen.close ();
        }
    }
    catch (...)
    {}

}


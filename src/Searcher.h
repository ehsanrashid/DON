//#pragma once
#ifndef SEARCHER_H_
#define SEARCHER_H_

//#include <memory>
//#include "Timer.h"
#include "Position.h"
#include "Evaluator.h"

inline Score mate_in (int32_t ply)
{
    return (SCORE_MATE - ply);
}

inline Score mated_in (int32_t ply)
{
    return (-SCORE_MATE + ply);
}

#include <iomanip>

inline std::ostream& operator<< (std::ostream &ostream, const Score &score)
{
    if (abs (int16_t (score)) < SCORE_INFINITE - 300)
    {
        ostream.setf (std::ios_base::fixed, std::ios_base::floatfield);
        ostream.setf (std::ios_base::right, std::ios_base::adjustfield);
        ostream << std::setiosflags (std::ios_base::showpos);
        ostream << std::setw (4) << std::setprecision (3) << (double) (score) / 1000;
        ostream.unsetf (std::ios_base::showpos);
    }
    else
    {
        ostream << ((score > 0) ? "+" : "-");
        int32_t value = SCORE_INFINITE - abs (int16_t (score));
        ostream << "MAT" << value;
        if (value < 10)
        {
            ostream << " ";
        }
    }
    return ostream;
}


namespace Searcher {


    //typedef std::unique_ptr<StateInfoStack>     StateInfoStackPtr;

    // GameClock stores the available time and time-gain per move
    typedef struct GameClock
    {
        int32_t time;   // unit: milli-seconds
        int32_t inc;    // unit: milli-seconds

        GameClock()
        {
            time  = 5 * 60 * 1000; // 5 mins default time
            inc   = 0;
        }

        GameClock(int32_t time_left, int32_t time_gain)
        {
            time  = time_left;
            inc   = time_gain;
        }

    } GameClock;

    // Limits stores information sent by GUI about available time to search the current move.
    //  - Maximum time and increment.
    //  - Maximum depth.
    //  - Maximum nodes.
    //  - Search move list
    //  - if in analysis mode.
    //  - if have to ponder while is opponent's side to move.
    typedef struct Limits
    {
        GameClock game_clock[CLR_NO];

        uint32_t  move_time;      // search time in milli-seconds
        uint8_t   moves_to_go;    // search x moves to the next time control
        uint8_t   depth;          // search x plies only
        uint16_t  nodes;          // search x nodes only
        uint8_t   mate_in;        // search mate in x moves
        MoveList  search_moves;   // search these moves only
        bool      infinite;       // search until the "stop" command
        bool      ponder;         // search on ponder move

        Limits() { std::memset (this, 0, sizeof (Limits)); }

        bool use_time_management () const
        {
            return !(mate_in | move_time | depth | nodes) && !infinite;
        }

        // Determines how much time it should search
        int time_to_search ()
        {
            //int cpuTime = board->turn == WHITE ? wTime : bTime;
            //int humanTime = board->turn == WHITE ? bTime : wTime;

            //int cpuInc = board->turn == WHITE ? wInc : bInc;

            //if (movesToGo > 0)
            //{
            //  return cpuTime / movesToGo + cpuInc / 2 + (cpuTime - humanTime) / 2;
            //}

            //return cpuTime / 30 + cpuInc / 2;
        }

    } Limits;

    // Signal stores volatile flags updated during the search sent by the GUI
    // typically in an async fashion.
    //  - Stop on ponderhit.
    //  - Stop Search. (to stop the search by the GUI)
    //  - First root move.
    //  - Falied low at root.
    typedef struct Signal
    {
        bool stop_on_ponderhit;
        bool stop_search;
        bool first_root_move;
        bool failed_low_at_root;

        Signal() { std::memset (this, 0, sizeof (Signal)); }

    } Signal;

    // RootMove is used for moves at the root of the tree.
    // For each root move stores:
    //  - Score,
    //  - Node count.
    //  - PV (really a refutation in the case of moves which fail low).
    // Score is normally set at -VALUE_INFINITE for all non-pv moves.
    struct RootMove
    {
        Score score;
        Score score_last;
        MoveList pv;

        RootMove(Move m) :
            score(-SCORE_INFINITE),
            score_last(-SCORE_INFINITE)
        {
            pv.emplace_back (m);
            pv.emplace_back (MOVE_NONE);
        }

        // Ascending Sort
        bool operator< (const RootMove &m) const
        {
            return score > m.score;
        }
        bool operator== (const Move &m) const
        {
            return pv[0] == m;
        }

        void extract_pv_from_tt (Position& pos);
        void insert_pv_into_tt (Position& pos);

    };

    const uint16_t MAX_DEPTH = 64;
    const uint16_t MAX_MOVES = 192;
    const uint16_t MAX_PLY   = 100;
    const uint16_t MAX_PLY_2 = MAX_PLY + 2;

    const uint16_t FAIL_LOW_MARGIN = 50; // => 20
    const uint16_t FUTILITY_CUT_LIMIT_PCT = 60; // => 60
    const uint16_t MAX_THREAT = 90;


    extern int nThreads;
    extern int maxThreadsPerNode;

    //typedef struct Node
    //{
    //    //Node* parent;
    //    //Node* children[maxThreads];
    //    Position pos;
    //
    //    //volatile bool _stopped;
    //    //volatile int workers;
    //    //bool used;
    //    //int threadNumber;
    //    //pthread_mutex_t mutex;
    //    //Variation pv;
    //    //int nodes;
    //    //int qNodes;
    //    //int failHighs;
    //    //int failHighFirsts;
    //    //int transRefProbes;
    //    //int transRefHits;
    //    //Move killer1[maxPly],killer2[maxPly];
    //    //Score pValue;
    //    //Score cValue;
    //    //Score alpha;
    //    //Score beta;
    //    //Depth depth;
    //    //int ply;
    //    //Depth totalExtension;
    //    //Moves moves[maxPly];
    //    //int nextMove[maxPly];
    //    //Move bestMove;
    //
    //} Node;
    //
    //
    //extern void Think(Node &rootNode);
    //
    //Score search(Node &node, Score alpha, Score beta, Depth depth, int ply, bool nullMoveIsOK, int8_t totalExtension);

    extern void search(const Position &pos, Depth depth);

}

#endif

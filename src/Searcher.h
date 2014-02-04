//#pragma once
#ifndef SEARCHER_H_
#define SEARCHER_H_

#include <iomanip>
#include <memory>
#include "Type.h"
#include "Time.h"
#include "Position.h"
#include "PolyglotBook.h"

#pragma warning (push)
#pragma warning (disable: 4805)

struct SplitPoint;

//typedef std::unique_ptr<StateInfoStack>   StateInfoStackPtr;
typedef std::auto_ptr<StateInfoStack>       StateInfoStackPtr;

namespace Searcher {

    const uint16_t MAX_DEPTH = 64;

    //const uint16_t FAIL_LOW_MARGIN = 50;        // => 20
    //const uint16_t FUTILITY_CUT_LIMIT_PCT = 60; // => 60
    //const uint16_t MAX_THREAT = 90;

    extern PolyglotBook book;

    // GameClock stores the available time and time-gain per move
    typedef struct GameClock
    {
        // unit: milli-seconds
        int32_t time;   // time left
        int32_t inc;    // time gain

        GameClock ()
        {
            time  = 0; //5 * 60 * MS_SEC; // 5 mins default time
            inc   = 0;
        }

        GameClock (int32_t tm, int32_t in)
        {
            time  = tm;
            inc   = in;
        }

    } GameClock;

    // Limits stores information sent by GUI about available time to search the current move.
    //  - Maximum time and increment.
    //  - Maximum depth.
    //  - Maximum nodes.
    //  - Search move list
    //  - if in analysis mode.
    //  - if have to ponder while is opponent's side to move.
    typedef struct LimitsT
    {
        GameClock game_clock[CLR_NO];

        uint32_t  move_time;      // search <x> time in milli-seconds
        uint8_t   moves_to_go;    // search <x> moves to the next time control
        uint8_t   depth;          // search <x> depth (plies) only
        uint16_t  nodes;          // search <x> nodes only
        uint8_t   mate_in;        // search mate in <x> moves
        bool      infinite;       // search until the "stop" command
        bool      ponder;         // search on ponder move

        std::vector<Move>  search_moves;   // search these moves only restrict

        LimitsT () { std::memset (this, 0, sizeof (LimitsT)); }

        bool use_time_management () const
        {
            return !(infinite | mate_in | move_time | depth | nodes);
        }

        // Determines how much time it should search
        //int32_t time_to_search ()
        //{
        //    int32_t cpu_time = board->turn == WHITE ? wTime : bTime;
        //    int32_t human_time = board->turn == WHITE ? bTime : wTime;
        //    int32_t cpu_inc = board->turn == WHITE ? wInc : bInc;
        //    if (moves_to_go > 0)
        //    {
        //      return cpu_time / moves_to_go + cpu_inc / 2 + (cpu_time - human_time) / 2;
        //    }
        //    return cpu_time / 30 + cpu_inc / 2;
        //}

    } LimitsT;

    // Signals stores volatile flags updated during the search sent by the GUI
    // typically in an async fashion.
    //  - Stop search (to stop the search by the GUI).
    //  - Stop on ponderhit.
    //  - On first root move.
    //  - Falied low at root.
    typedef struct SignalsT
    {
        bool stop;
        bool stop_on_ponderhit;
        bool first_root_move;
        bool failed_low_at_root;

        SignalsT () { std::memset (this, 0, sizeof (SignalsT)); }

    } SignalsT;

    // PV, CUT, and ALL nodes, respectively. The root of the tree is a PV node. At a PV
    // node all the children have to be investigated. The best move found at a PV node
    // leads to a successor PV node, while all the other investigated children are CUT
    // nodes. At a CUT node the child causing aﬂcut-off is an ALL node. In a perfectly
    // ordered tree only one child of a CUT node has to be explored. At an ALL node all
    // the children have to be explored. The successors of an ALL node are CUT nodes.
    // NonPV = CUT + ALL
    // Different node types, used as template parameter
    enum NodeT { Root, PV, NonPV, SplitPointRoot, SplitPointPV, SplitPointNonPV };

    // RootMove is used for moves at the root of the tree.
    // For each root move stores:
    //  - Current value.
    //  - Last value.
    //  - Node count.
    //  - PV (really a refutation table in the case of moves which fail low).
    // Score is normally set at -VALUE_INFINITE for all non-pv moves.
    struct RootMove
    {
        Value curr_value;
        Value last_value;
        //uint64_t nodes;

        std::vector<Move> pv;

        RootMove (Move m)
            : curr_value (-VALUE_INFINITE)
            , last_value (-VALUE_INFINITE)
        {
            pv.push_back (m);
            pv.push_back (MOVE_NONE);
        }

        // Ascending Sort

        friend bool operator<  (const RootMove &rm1, const RootMove &rm2) { return (rm1.curr_value >  rm2.curr_value); }
        friend bool operator>  (const RootMove &rm1, const RootMove &rm2) { return (rm1.curr_value <  rm2.curr_value); }
        friend bool operator<= (const RootMove &rm1, const RootMove &rm2) { return (rm1.curr_value >= rm2.curr_value); }
        friend bool operator>= (const RootMove &rm1, const RootMove &rm2) { return (rm1.curr_value <= rm2.curr_value); }
        friend bool operator== (const RootMove &rm1, const RootMove &rm2) { return (rm1.curr_value == rm2.curr_value); }
        friend bool operator!= (const RootMove &rm1, const RootMove &rm2) { return (rm1.curr_value != rm2.curr_value); }

        friend bool operator== (const RootMove &rm, const Move &m) { return (rm.pv[0] == m); }
        friend bool operator!= (const RootMove &rm, const Move &m) { return (rm.pv[0] != m); }

        void extract_pv_from_tt (Position &pos);
        void  insert_pv_into_tt (Position &pos);

    };

    // The Stack struct keeps track of the information we need to remember from
    // nodes shallower and deeper in the tree during the search. Each search thread
    // has its own array of Stack objects, indexed by the current ply.
    typedef struct Stack
    {
        SplitPoint *split_point;
        uint8_t     ply;
        Move        current_move;
        Move        tt_move;
        Move        excluded_move;
        Move        killers[2];
        Depth       reduction;
        Value       static_eval;
        bool        skip_null_move;

        //uint8_t     null_move_count;
        //uint8_t     null_cut_count; //Keep track of the moves causing a cut-off at d-R

    } Stack;

    extern LimitsT              Limits;
    extern volatile SignalsT    Signals;

    extern std::vector<RootMove> RootMoves;
    extern Position              RootPos;
    extern Color                 RootColor;
    extern StateInfoStackPtr     SetupStates;

    extern Time::point           SearchTime;

    extern uint64_t perft (Position &pos, Depth depth);

    extern void think ();

    extern void initialize ();

}

#pragma warning (pop)

#endif // SEARCHER_H_

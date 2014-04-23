#ifdef _MSC_VER
#   pragma once
#endif

#ifndef SEARCHER_H_INC_
#define SEARCHER_H_INC_

#include <cstring>
#include <memory>
#include <vector>

#include "Type.h"
#include "Time.h"
#include "Position.h"
#include "PolyglotBook.h"

#ifdef _MSC_VER
#   pragma warning (push)
#   pragma warning (disable: 4805)
#endif

namespace Threads {
    struct SplitPoint;
}

typedef std::auto_ptr<StateInfoStack>       StateInfoStackPtr;

namespace Searcher {

    using namespace Threads;

    const u08 MAX_SKILL_LEVEL   = 20;
    const u08 MIN_SKILL_MULTIPV =  4;

    // Limits stores information sent by GUI about available time to search the current move.
    //  - Maximum time and increment.
    //  - Maximum depth.
    //  - Maximum nodes.
    //  - Search move list
    //  - Infinite analysis mode.
    //  - Ponder while is opponent's side to move.
    struct LimitsT
    {

    private:
        // GameClock stores the available time and time-gain per move
        struct GameClock
        {
            // unit: milli-seconds
            u32 time;   // Time left
            u32 inc;    // Time gain

            GameClock ()
                : time (0)
                , inc  (0)
            {}
        };

    public:

        GameClock gameclock[CLR_NO];
        std::vector<Move>  searchmoves;   // search these moves only restrict

        u32  movetime;  // search <x> time in milli-seconds
        u08  movestogo; // search <x> moves to the next time control
        u08  depth;     // search <x> depth (plies) only
        u32  nodes;     // search <x> nodes only
        u08  mate;      // search mate in <x> moves
        bool infinite;  // search until the "stop" command
        bool ponder;    // search on ponder move

        LimitsT ()
            : movetime  (0)
            , movestogo (0)
            , depth     (0)
            , nodes     (0)
            , mate      (0)
            , infinite  (false)
            , ponder    (false)
        {}
        
        bool use_timemanager () const
        {
            return !(infinite || movetime || depth || nodes || mate);
        }
    };

    // Signals stores volatile flags updated during the search sent by the GUI
    // typically in an async fashion.
    //  - Stop search (to stop the search by the GUI).
    //  - Stop on ponderhit.
    //  - On first root move.
    //  - Falied low at root.
    struct SignalsT
    {
        bool  stop              // Stop any way
            , stop_ponderhit    // Stop on Ponder hit
            , root_1stmove      // First RootMove
            , root_failedlow;   // Failed low at Root

        SignalsT ()
            //: stop           (false)
            //, stop_ponderhit (false)
            //, root_1stmove   (false)
            //, root_failedlow (false)
        {
            memset (this, 0x00, sizeof (*this));
        }
    };

    // PV, CUT & ALL nodes, respectively. The root of the tree is a PV node. At a PV node
    // all the children have to be investigated. The best move found at a PV node leads
    // to a successor PV node, while all the other investigated children are CUT nodes
    // At a CUT node the child causing a beta cut-off is an ALL node. In a perfectly
    // ordered tree only one child of a CUT node has to be explored. At an ALL node all
    // the children have to be explored. The successors of an ALL node are CUT nodes.
    // NonPV nodes = CUT nodes + ALL nodes
    // Node types, used as template parameter
    enum NodeT { Root, PV, NonPV, SplitPointRoot, SplitPointPV, SplitPointNonPV };

    // RootMove is used for moves at the root of the tree.
    // For each root move stores:
    //  - Value[] { new , old }.
    //  - Node count.
    //  - PV (really a refutation table in the case of moves which fail low).
    // Value is normally set at -VALUE_INFINITE for all non-pv moves.
    struct RootMove
    {
        Value value[2];
        //u64   nodes;
        std::vector<Move> pv;

        RootMove (Move m = MOVE_NONE)
            //: nodes (U64 (0))
        {
            value[0] = -VALUE_INFINITE;
            value[1] = -VALUE_INFINITE;
            pv.push_back (m);
            pv.push_back (MOVE_NONE);
        }

        // Ascending Sort

        friend bool operator<  (const RootMove &rm1, const RootMove &rm2) { return (rm1.value[0] >  rm2.value[0]); }
        friend bool operator>  (const RootMove &rm1, const RootMove &rm2) { return (rm1.value[0] <  rm2.value[0]); }
        friend bool operator<= (const RootMove &rm1, const RootMove &rm2) { return (rm1.value[0] >= rm2.value[0]); }
        friend bool operator>= (const RootMove &rm1, const RootMove &rm2) { return (rm1.value[0] <= rm2.value[0]); }
        friend bool operator== (const RootMove &rm1, const RootMove &rm2) { return (rm1.value[0] == rm2.value[0]); }
        friend bool operator!= (const RootMove &rm1, const RootMove &rm2) { return (rm1.value[0] != rm2.value[0]); }

        friend bool operator== (const RootMove &rm, const Move &m) { return (rm.pv[0] == m); }
        friend bool operator!= (const RootMove &rm, const Move &m) { return (rm.pv[0] != m); }

        void extract_pv_from_tt (Position &pos);
        void  insert_pv_into_tt (Position &pos);

    };

    // The Stack struct keeps track of the information we need to remember from
    // nodes shallower and deeper in the tree during the search. Each search thread
    // has its own array of Stack objects, indexed by the current ply.
    struct Stack
    {
        SplitPoint *splitpoint;

        Move    current_move
            ,   tt_move
            ,   excluded_move;

        Depth   reduction;

        Move    killer_moves[2];
        
        u08     ply;

        Value   static_eval;
        bool    skip_null_move;

    };

    extern LimitsT               Limits;
    extern SignalsT volatile     Signals;

    extern std::vector<RootMove> RootMoves;
    extern Position              RootPos;
    extern Color                 RootColor;
    extern StateInfoStackPtr     SetupStates;

    extern Time::point           SearchTime;

    extern PolyglotBook          Book;

    extern u64 perft (Position &pos, const Depth &depth);

    extern void think ();

    extern void initialize ();

}

#ifdef _MSC_VER
#   pragma warning (pop)
#endif

#endif // SEARCHER_H_INC_

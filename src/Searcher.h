#ifndef SEARCHER_H_INC_
#define SEARCHER_H_INC_

#include <cstring>
#include <memory>

#include "Type.h"
#include "Time.h"
#include "Position.h"
#include "PolyglotBook.h"

namespace Threads {
    struct SplitPoint;
}

typedef std::auto_ptr<StateInfoStack>       StateInfoStackPtr;

namespace Search {

    using namespace Threads;

    const u08 MAX_SKILL_LEVEL   = 32; // MAX_SKILL_LEVEL should be < MAX_DEPTH/2
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
    public:

        GameClock gameclock[CLR_NO];
        std::vector<Move> root_moves;   // restrict search to these moves only

        u32  movetime;  // search <x> time in milli-seconds
        u08  movestogo; // search <x> moves to the next time control
        u08  depth;     // search <x> depth (plies) only
        u64  nodes;     // search <x> nodes only
        u08  mate;      // search mate in <x> moves
        bool ponder;    // search on ponder move
        bool infinite;  // search until the "stop" command

        LimitsT ()
            : movetime  (0)
            , movestogo (0)
            , depth     (0)
            , nodes     (0)
            , mate      (0)
            , ponder    (false)
            , infinite  (false)
        {}
        
        bool use_timemanager () const
        {
            return !(infinite || movetime || depth || nodes || mate);
        }
    };

    // Signals stores volatile flags updated during the search sent by the GUI
    // typically in an async fashion.
    //  - Stop search on request.
    //  - Stop search on ponderhit.
    //  - First root move.
    //  - Falied low at root.
    struct SignalsT
    {
        bool  force_stop        // Stop on request
            , ponderhit_stop    // Stop on ponder-hit
            , root_1stmove      // First move at root
            , root_failedlow;   // Failed-low move at root

        SignalsT ()
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
    enum NodeT { Root, PV, NonPV };

    // RootMove is used for moves at the root of the tree.
    // For each root move stores:
    //  - Value[] { new , old }.
    //  - Node count.
    //  - PV (really a refutation table in the case of moves which fail low).
    // Value is normally set at -VALUE_INFINITE for all non-pv moves.
    struct RootMove
    {
        Value value[2];
        u64   nodes;
        std::vector<Move> pv;

        explicit RootMove (Move m = MOVE_NONE)
            : nodes (U64(0))
        {
            value[0] = -VALUE_INFINITE;
            value[1] = -VALUE_INFINITE;
            pv.push_back (m);
            if (m != MOVE_NONE) pv.push_back (MOVE_NONE);
        }
        
        //RootMove (const RootMove &rm) { *this = rm; }
        //RootMove& RootMove::operator= (const RootMove &rm)
        //{
        //    nodes    = rm.nodes;
        //    value[0] = rm.value[0];
        //    value[1] = rm.value[1];
        //    pv       = rm.pv;
        //    return *this;
        //}

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

        std::string info_pv (const Position &pos) const;
    };

    class RootMoveList
        : public std::vector<RootMove>
    {

    public:
        float best_move_change;

        void initialize (const Position &pos, const std::vector<Move> &root_moves);

        //inline void sort_full ()     { std::stable_sort (begin (), end ()); }
        //inline void sort_beg (i32 n) { std::stable_sort (begin (), begin () + n); }
        //inline void sort_end (i32 n) { std::stable_sort (begin () + n, end ()); }
        
        //u64 game_nodes () const
        //{
        //    u64 nodes = U64(0);
        //    for (const_iterator itr = begin (); itr != end (); ++itr)
        //    {
        //        nodes += itr->nodes;
        //    }
        //    return nodes;
        //}
    };

    // The Stack struct keeps track of the information needed to remember from
    // nodes shallower and deeper in the tree during the search. Each search thread
    // has its own array of Stack objects, indexed by the current ply.
    struct Stack
    {
        SplitPoint *splitpoint;

        u08     ply;

        Move    tt_move
            ,   current_move
            ,   exclude_move
            ,   killer_moves[2];

        Value   static_eval;

    };

    extern LimitsT               Limits;
    extern SignalsT volatile     Signals;

    extern RootMoveList          RootMoves;
    extern Position              RootPos;
    extern StateInfoStackPtr     SetupStates;

    extern Time::point           SearchTime;

    extern OpeningBook::PolyglotBook Book;
    
    extern u64 perft (Position &pos, Depth depth);

    extern void think ();

    extern void initialize ();

}

#endif // SEARCHER_H_INC_

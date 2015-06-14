#ifndef SEARCHER_H_INC_
#define SEARCHER_H_INC_

#include <cstring>
#include <memory>

#include "Type.h"
#include "Position.h"
#include "PolyglotBook.h"

namespace Threading {
    struct SplitPoint;
}

typedef std::unique_ptr<StateInfoStack>   StateInfoStackPtr;

namespace Searcher {

    using namespace Threading;

    const u08 MAX_SKILL_LEVEL   = 32; // MAX_SKILL_LEVEL should be < MAX_DEPTH/2
    const u16 MIN_SKILL_MULTIPV =  4;

    typedef std::vector<Move> MoveVector;
    
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
        // Clock struct stores the remain-time and time-inc per move in milli-seconds
        struct Clock
        {
            u32 time;   // Remaining Time    [milli-seconds]
            u32 inc;    // Time inc per move [milli-seconds]

            Clock ()
                : time (0)
                , inc  (0)
            {}
        };

    public:

        Clock clock[CLR_NO];
        MoveVector root_moves; // restrict search to these moves only

        u32  movetime;  // search <x> time in milli-seconds
        u08  movestogo; // search <x> moves to the next time control
        u08  depth;     // search <x> depth (plies) only
        u64  nodes;     // search <x> nodes only
        u08  mate;      // search mate in <x> moves
        u32  npmsec;
        bool ponder;    // search on ponder move
        bool infinite;  // search until the "stop" command

        LimitsT ()
            : movetime  (0)
            , movestogo (0)
            , depth     (0)
            , nodes     (0)
            , mate      (0)
            , npmsec    (0)
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
            , firstmove_root    // Move is First at root
            , failedlow_root;   // Move Failed-low at root

        SignalsT ()
            : force_stop (false)
            , ponderhit_stop (false)
            , firstmove_root (false)
            , failedlow_root (false)
        {}
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
    class RootMove
    {
    public:

        Value      new_value
            ,      old_value;
        MoveVector pv;
        u64        nodes;

        explicit RootMove (Move m = MOVE_NONE)
            : new_value (-VALUE_INFINITE)
            , old_value (-VALUE_INFINITE)
            , pv (1, m)
            , nodes (U64(0))
        {}
        
        // Ascending Sort
        friend bool operator<  (const RootMove &rm1, const RootMove &rm2) { return rm1.new_value >  rm2.new_value; }
        friend bool operator>  (const RootMove &rm1, const RootMove &rm2) { return rm1.new_value <  rm2.new_value; }
        friend bool operator<= (const RootMove &rm1, const RootMove &rm2) { return rm1.new_value >= rm2.new_value; }
        friend bool operator>= (const RootMove &rm1, const RootMove &rm2) { return rm1.new_value <= rm2.new_value; }

        friend bool operator== (const RootMove &rm1, const RootMove &rm2) { return rm1.new_value == rm2.new_value; }
        friend bool operator!= (const RootMove &rm1, const RootMove &rm2) { return rm1.new_value != rm2.new_value; }

        friend bool operator== (const RootMove &rm, Move m) { return rm.pv[0] == m; }
        friend bool operator!= (const RootMove &rm, Move m) { return rm.pv[0] != m; }

        void insert_pv_into_tt (Position &pos);
        bool ponder_move_extracted_from_tt (Position &pos);

        operator std::string () const;
        
        template<class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>&
            operator<< (std::basic_ostream<CharT, Traits> &os, const RootMove &rm)
        {
            os << std::string(rm);
            return os;
        }

    };

    class RootMoveVector
        : public std::vector<RootMove>
    {

    public:
        void initialize (const Position &pos, const MoveVector &root_moves);
        void initialize (const Position &pos)
        {
            MoveVector root_moves;
            initialize (pos, root_moves);
        }

        //u64 game_nodes () const
        //{
        //    u64 nodes = U64(0);
        //    for (const RootMove &rm : *this)
        //    {
        //        nodes += rm.nodes;
        //    }
        //    return nodes;
        //}
    };

    struct Skill
    {

    private:
        u08  _level;
        Move _best_move;

    public:

        explicit Skill (u08 level = MAX_SKILL_LEVEL)
        {
            change_level (level);
        }

        void change_level (u08 level) { _level = level; }

        void clear () { _best_move = MOVE_NONE; }

        bool can_pick_move (Depth depth) const { return depth/DEPTH_ONE == 1 + _level; }

        u16  pv_size () const;

        Move pick_move ();

        void play_move ();

    };

    extern bool                 Chess960;

    extern LimitsT              Limits;
    extern SignalsT volatile    Signals;

    extern Position             RootPos;
    extern RootMoveVector       RootMoves;
    extern StateInfoStackPtr    SetupStates;

    extern u16                  MultiPV;
    //extern i32                MultiPV_cp;

    extern i16                  FixedContempt
        ,                       ContemptTime 
        ,                       ContemptValue;

    extern std::string          HashFile;
    extern u16                  AutoSaveHashTime;
    extern bool                 AutoLoadHash;

    extern std::string          BookFile;
    extern bool                 BestBookMove;
    extern OpeningBook::PolyglotBook Book;

    extern std::string          SearchLog;
    
    extern Skill                Skills;

    // The Stack struct keeps track of the information needed to remember from
    // nodes shallower and deeper in the tree during the search. Each search thread
    // has its own array of Stack objects, indexed by the current ply.
    struct Stack
    {
        SplitPoint *splitpoint;
        Move       *pv;
        i32         ply;

        Move    tt_move
            ,   current_move
            ,   exclude_move
            ,   killer_moves[2];

        Value   static_eval;

    };

    // TimeManager class computes the optimal time to think depending on the
    // maximum available time, the move game number and other parameters.
    // Support four different kind of time controls, passed in 'limits':
    //
    // moves_to_go = 0, increment = 0 means: x basetime  [sudden death!]
    // moves_to_go = 0, increment > 0 means: x basetime + z increment
    // moves_to_go > 0, increment = 0 means: x moves in y basetime
    // moves_to_go > 0, increment > 0 means: x moves in y basetime + z increment
    class TimeManager
    {
    private:

        TimePoint _start_time;
        u32       _optimum_time;
        u32       _maximum_time;

        double    _instability_factor;

    public:

        u64     available_nodes; // When in 'nodes as time' mode
        double  best_move_change;

        u32 available_time () const { return u32(_optimum_time * _instability_factor * 0.76); }
    
        u32 maximum_time   () const { return _maximum_time; }

        u32 elapsed_time   () const { return u32(Limits.npmsec != 0 ? RootPos.game_nodes () : now () - _start_time); }

        void instability   ()       { _instability_factor = 1.0 + best_move_change; }

        void initialize (Color c, LimitsT &limits, i32 game_ply, TimePoint now_time);

    };

    extern u08  MaximumMoveHorizon  ;
    extern u08  EmergencyMoveHorizon;
    extern u32  EmergencyClockTime  ;
    extern u32  EmergencyMoveTime   ;
    extern u32  MinimumMoveTime     ;
    extern i32  MoveSlowness        ;
    extern i32  NodesTime           ;
    extern bool Ponder              ;

    extern TimeManager TimeMgr;


    extern u64  perft (Position &pos, Depth depth);

    extern void think ();
    extern void reset ();

    extern void initialize ();

}

#endif // SEARCHER_H_INC_

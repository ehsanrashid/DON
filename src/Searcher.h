#ifndef SEARCHER_H_INC_
#define SEARCHER_H_INC_

#include <cstring>
#include <memory>
#include <atomic>

#include "Type.h"
#include "Position.h"

typedef std::unique_ptr<StateStack> StateStackPtr;

namespace Searcher {

    using namespace Threading;

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
        // Clock struct stores the Remaining-time and Increment-time per move in milli-seconds
        struct Clock
        {
            u32 time    = 0; // Remaining Time          [milli-seconds]
            u32 inc     = 0; // Increment Time per move [milli-seconds]
        };

    public:

        Clock clock[CLR_NO];
        MoveVector root_moves; // Restrict search to these moves only
        TimePoint  start_time;

        u32  movetime   = 0; // Search <x> exact time in milli-seconds
        u08  movestogo  = 0; // Search <x> moves to the next time control
        u08  depth      = 0; // Search <x> depth (plies) only
        u64  nodes      = 0; // Search <x> nodes only
        u08  mate       = 0; // Search mate in <x> moves
        u32  npmsec     = 0;
        bool ponder     = false; // Search on ponder move until the "stop" command
        bool infinite   = false; // Search until the "stop" command
        
        bool use_time_manager () const
        {
            return !(infinite || movetime || depth || nodes || mate);
        }
    };

    // Signals stores atomic flags updated during the search sent by the GUI
    // typically in an async fashion.
    //  - Stop search on request.
    //  - Stop search on ponder-hit.
    //  - First move at root.
    //  - Falied low at root.
    struct SignalsT
    {
        std::atomic_bool
              force_stop        { false }  // Stop search on request
            , ponderhit_stop    { false }  // Stop search on ponder-hit
            , firstmove_root    { false }  // First move at root
            , failedlow_root    { false }; // Failed low at root
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

        Value new_value = -VALUE_INFINITE
            , old_value = -VALUE_INFINITE;
        MoveVector pv;

        explicit RootMove (Move m = MOVE_NONE) : pv (1, m) {}

        bool operator<  (const RootMove &rm) const { return new_value >  rm.new_value; }
        bool operator>  (const RootMove &rm) const { return new_value <  rm.new_value; }
        bool operator<= (const RootMove &rm) const { return new_value >= rm.new_value; }
        bool operator>= (const RootMove &rm) const { return new_value <= rm.new_value; }
        bool operator== (const RootMove &rm) const { return new_value == rm.new_value; }
        bool operator!= (const RootMove &rm) const { return new_value != rm.new_value; }

        bool operator== (Move m) const { return pv[0] == m; }
        bool operator!= (Move m) const { return pv[0] != m; }

        Move operator[] (i32 index) const { return pv[index]; }

        void operator+= (Move m) { pv.push_back (m); }
        void operator-= (Move m) { pv.erase (std::remove (pv.begin (), pv.end (), m), pv.end ()); }

        size_t size () const { return pv.size (); }

        void backup () { old_value = new_value; }
        void insert_pv_into_tt (Position &pos);
        bool extract_ponder_move_from_tt (Position &pos);

        operator std::string () const;

        template<class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>&
            operator<< (std::basic_ostream<CharT, Traits> &os, const RootMove &rm)
        {
            os << std::string (rm);
            return os;
        }

    };

    class RootMoveVector
        : public std::vector<RootMove>
    {

    public:

        void operator+= (const RootMove &rm) { push_back (rm); }
        void operator-= (const RootMove &rm) { erase (std::remove (begin (), end (), rm), end ()); }

        void backup ()
        {
            for (auto &rm : *this)
            {
                rm.backup ();
            }
        }

        void initialize (const Position &pos, const MoveVector &root_moves);

        operator std::string () const;

        template<class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>&
            operator<< (std::basic_ostream<CharT, Traits> &os, const RootMoveVector &rmv)
        {
            os << std::string (rmv);
            return os;
        }
    };

    // The Stack struct keeps track of the information needed to remember from
    // nodes shallower and deeper in the tree during the search. Each search thread
    // has its own array of Stack objects, indexed by the current ply.
    struct Stack
    {
        Move *pv = nullptr;
        u16  ply = 0;

        Move tt_move      = MOVE_NONE
            , current_move = MOVE_NONE
            , exclude_move = MOVE_NONE
            , killer_moves[2];

        Value static_eval = VALUE_NONE;
        bool firstmove_pv = false;
    };

    const u08 MAX_SKILL_LEVEL   = 32; // MAX_SKILL_LEVEL should be <= MAX_DEPTH/4
    // Skill Manager
    class SkillManager
    {

    private:
        u08  _level     = MAX_SKILL_LEVEL;
        Move _best_move = MOVE_NONE;

    public:
        
        static const u16 SkillMultiPV = 4;

        void change_level (u08 level) { _level = level; }

        void clear () { _best_move = MOVE_NONE; }

        bool enabled () const { return _level < MAX_SKILL_LEVEL; }

        bool depth_to_pick (Depth depth) const { return depth/DEPTH_ONE == (1 + _level); }

        Move best_move (const RootMoveVector &root_moves) { return _best_move != MOVE_NONE ? _best_move : pick_best_move (root_moves); }
        
        Move pick_best_move (const RootMoveVector &root_moves);

    };

    // TimeManager class computes the optimal time to think depending on the
    // maximum available time, the move game number and other parameters.
    // Support four different kind of time controls, passed in 'limits':
    //
    // moves_to_go = 0, increment = 0 means: x basetime [sudden death!]
    // moves_to_go = 0, increment > 0 means: x basetime + z increment
    // moves_to_go > 0, increment = 0 means: x moves in y basetime [regular clock]
    // moves_to_go > 0, increment > 0 means: x moves in y basetime + z increment
    class TimeManager
    {
    private:

        TimePoint _start_time   = 0;
        u32       _optimum_time = 0;
        u32       _maximum_time = 0;

        double    _instability_factor = 1.0;

    public:

        u64     available_nodes  = 0; // When in 'nodes as time' mode
        double  best_move_change = 0.0;

        u32 available_time () const { return u32 (_optimum_time * _instability_factor * 0.76); }

        u32 maximum_time () const { return _maximum_time; }

        u32 elapsed_time () const;

        void instability () { _instability_factor = 1.0 + best_move_change; }

        void initialize ();

    };


    extern bool             Chess960;

    extern LimitsT          Limits;
    extern SignalsT         Signals;
    extern StateStackPtr    SetupStates;

    extern u16              MultiPV;
    //extern i32              MultiPV_cp;

    extern i16              FixedContempt
        ,                   ContemptTime 
        ,                   ContemptValue;

    extern std::string      HashFile;
    extern u16              AutoSaveHashTime;
    
    extern bool             OwnBook;
    extern std::string      BookFile;
    extern bool             BookMoveBest;

    extern std::string      SearchFile;

    extern SkillManager     SkillMgr;

    extern u08  MaximumMoveHorizon;
    extern u08  ReadyMoveHorizon  ;
    extern u32  OverheadClockTime ;
    extern u32  OverheadMoveTime  ;
    extern u32  MinimumMoveTime   ;
    extern u32  MoveSlowness      ;
    extern u32  NodesTime         ;
    extern bool Ponder            ;

    extern TimeManager TimeMgr;

    template<bool RootNode = true>
    extern u64 perft (Position &pos, Depth depth);

    extern void initialize ();
    extern void clear ();
}

#endif // SEARCHER_H_INC_

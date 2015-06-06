#ifndef _TIME_MANAGER_H_INC_
#define _TIME_MANAGER_H_INC_

#include "Type.h"
#include "Searcher.h"
#include "UCI.h"

namespace TimeManagement {

    // TimeManager class computes the optimal time to think depending on the
    // maximum available time, the move game number and other parameters.
    // Support four different kind of time controls:
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

        u64 available_nodes; // When in 'nodes as time' mode

        u32 available_time () const { return u32(_optimum_time * _instability_factor * 0.76); }
    
        u32 maximum_time   () const { return _maximum_time; }

        u32 elapsed_time   () const { return u32(Searcher::Limits.npmsec ? Searcher::RootPos.game_nodes () : now () - _start_time); }

        void instability (double best_move_change) { _instability_factor = 1.0 + best_move_change; }

        void initialize (Color c, Searcher::LimitsT &limits, i32 game_ply, TimePoint now_time);

    };

    extern u08  MaximumMoveHorizon  ;
    extern u08  EmergencyMoveHorizon;
    extern u32  EmergencyClockTime  ;
    extern u32  EmergencyMoveTime   ;
    extern u32  MinimumMoveTime     ;
    extern i32  MoveSlowness        ;
    extern i32  NodesTime           ;
    extern bool Ponder              ;

}

extern TimeManagement::TimeManager TimeMgr;

#endif // _TIME_MANAGER_H_INC_

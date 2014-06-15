#ifndef _TIME_MANAGER_H_INC_
#define _TIME_MANAGER_H_INC_

#include "Type.h"

// TimeManager class computes the optimal time to think depending on the
// maximum available time, the move game number and other parameters.
//Support four different kind of time controls:
//
//increment == 0 && moves_to_go == 0 means: x basetime  [sudden death!]
//increment == 0 && moves_to_go != 0 means: x moves in y minutes
//increment >  0 && moves_to_go == 0 means: x basetime + z increment
//increment >  0 && moves_to_go != 0 means: x moves in y minutes + z increment
class TimeManager
{

private:

    u32    _optimum_time;
    u32    _maximum_time;
    double _unstable_pv_factor;

public:

    inline u32 available_time () const { return u32 (_optimum_time * _unstable_pv_factor * 0.71); }
    
    inline u32 maximum_time   () const { return _maximum_time; }

    inline void pv_instability (double best_move_changes)
    {
        _unstable_pv_factor = 1 + best_move_changes;
    }

    void initialize (const GameClock &gameclock, u08 movestogo, i32 game_ply);
    
};

#endif // _TIME_MANAGER_H_INC_

#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TIME_MANAGER_H_INC_
#define _TIME_MANAGER_H_INC_

#include "Searcher.h"

// TimeManager class computes the optimal time to think depending on the
// maximum available time, the move game number and other parameters.
class TimeManager
{

private:

    u32    _optimum_search_time;
    u32    _maximum_search_time;
    double _unstable_pv_factor;

public:

    inline u32 available_time () const { return _optimum_search_time * _unstable_pv_factor * 0.71; }
    
    inline u32 maximum_time   () const { return _maximum_search_time; }

    inline void pv_instability (double best_move_changes)
    {
        _unstable_pv_factor = 1 + best_move_changes;
    }

    void initialize (const Searcher::LimitsT &limits, u16 game_ply, Color c);
    
};

#endif // _TIME_MANAGER_H_INC_

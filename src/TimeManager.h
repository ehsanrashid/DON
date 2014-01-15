//#pragma once
#ifndef TIME_MANAGER_H_
#define TIME_MANAGER_H_

#include "Searcher.h"

// TimeManager class computes the optimal time to think depending on the
// maximum available time, the move game number and other parameters.
typedef class TimeManager
{
private:
    int32_t _optimum_search_time;
    int32_t _maximum_search_time;
    int32_t _unstable_pv_extra_time;

public:

    inline int32_t available_time () const { return _optimum_search_time + _unstable_pv_extra_time; }
    inline int32_t maximum_time   () const { return _maximum_search_time; }

    inline void pv_instability (double best_move_changes)
    {
        _unstable_pv_extra_time = int32_t (best_move_changes * _optimum_search_time / 1.4);
    }

    void initialize (const Searcher::Limits_t &limits, int32_t current_ply, Color c);
    
} TimeManager;

#endif
//#pragma once
#ifndef TIME_MANAGER_H_
#define TIME_MANAGER_H_

#include "Searcher.h"

// TimeManager class computes the optimal time to think depending on the
// maximum available time, the move game number and other parameters.
typedef class TimeManager
{

private:

    uint32_t _optimum_search_time;
    uint32_t _maximum_search_time;
    double  _unstable_pv_factor;

public:

    inline uint32_t available_time () const { return _optimum_search_time + _unstable_pv_factor * 0.62; }
    
    inline uint32_t maximum_time   () const { return _maximum_search_time; }

    inline void pv_instability (double best_move_changes)
    {
        _unstable_pv_factor = 1 + best_move_changes;
    }

    void initialize (const Searcher::LimitsT &limits, uint16_t current_ply, Color c);
    
} TimeManager;

#endif // TIME_MANAGER_H_

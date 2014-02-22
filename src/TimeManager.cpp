#include "TimeManager.h"

#include <cfloat>
#include <cmath>
#include <algorithm>

#include "UCI.h"

using namespace std;
using namespace Searcher;

namespace {

    const uint8_t MoveHorizon   = 50;    // Plan time management at most this many moves ahead
    const double  MaxRatio      =  7.0;  // When in trouble, we can step over reserved time with this ratio
    const double  StealRatio    =  0.33; // However we must not steal time from remaining moves over this ratio

    const double Scale          =  9.3;
    const double Shift          = 59.8;
    const double SkewFactor     =  0.172;

    // move_importance() is a skew-logistic function based on naive statistical
    // analysis of "how many games are still undecided after n half-moves".
    // Game is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from CCRL game database with some simple filtering criteria.
    inline double move_importance (uint16_t ply)
    {
        return pow ((1 + exp ((ply - Shift) / Scale)), -SkewFactor) + DBL_MIN; // Ensure non-zero
    }

    typedef enum TimeT { OPTIMUM_TIME, MAXIMUM_TIME } TimeT;

    // remaining_time() calculate the time remaining
    template<TimeT TT>
    inline uint32_t remaining_time (uint32_t time, uint8_t moves_to_go, uint16_t current_ply, uint16_t slow_mover)
    {
        double  curr_moves_importance = (move_importance (current_ply) * slow_mover) / 100;
        double other_moves_importance = 0.0;

        for (uint8_t i = 1; i < moves_to_go; ++i)
        {
            other_moves_importance += move_importance (current_ply + 2 * i);
        }

        double time_ratio1;
        double time_ratio2;

        if      (OPTIMUM_TIME == TT)
        {
            time_ratio1 = (curr_moves_importance) / (curr_moves_importance + other_moves_importance);
            time_ratio2 = (curr_moves_importance) / (curr_moves_importance + other_moves_importance);
        }
        else if (MAXIMUM_TIME == TT)
        {
            time_ratio1 = (MaxRatio * curr_moves_importance) / (MaxRatio * curr_moves_importance + other_moves_importance);
            time_ratio2 = (curr_moves_importance + StealRatio * other_moves_importance) / (curr_moves_importance + other_moves_importance);
        }

        return uint32_t (floor (time * min (time_ratio1, time_ratio2)));
    }

}

void TimeManager::initialize (const LimitsT &limits, uint16_t current_ply, Color c)
{
    /*
    We support four different kind of time controls:

    increment == 0 && moves_to_go == 0 means: x basetime  [sudden death!]
    increment == 0 && moves_to_go != 0 means: x moves in y minutes
    increment >  0 && moves_to_go == 0 means: x basetime + z increment
    increment >  0 && moves_to_go != 0 means: x moves in y minutes + z increment

    Time management is adjusted by following UCI parameters:

    emergency_move_horizon : Be prepared to always play at least this many moves
    emergency_base_time    : Always attempt to keep at least this much time (in ms) at clock
    emergency_move_time    : Plus attempt to keep at least this much time for each remaining emergency move
    min_thinking_time      : No matter what, use at least this much thinking before doing the move
    slow_mover             :
    */

    // Read uci parameters
    uint8_t  emergency_move_horizon = int32_t (*(Options["Emergency Move Horizon"]));
    uint32_t emergency_base_time    = int32_t (*(Options["Emergency Base Time"]));
    uint32_t emergency_move_time    = int32_t (*(Options["Emergency Move Time"]));
    uint32_t min_thinking_time      = int32_t (*(Options["Minimum Thinking Time"]));
    uint16_t slow_mover             = int32_t (*(Options["Slow Mover"]));

    // Initialize to maximum values but unstable_pv_extra_time that is reset
    _unstable_pv_factor  = 1.0;
    _optimum_search_time = _maximum_search_time = max (limits.game_clock[c].time, min_thinking_time);

    // We calculate optimum time usage for different hypothetic "moves to go"-values and choose the
    // minimum of calculated search time values. Usually the greatest hyp_moves_to_go gives the minimum values.
    for (uint8_t hyp_moves_to_go = 1;
        hyp_moves_to_go <= (limits.moves_to_go ? min (limits.moves_to_go, MoveHorizon) : MoveHorizon);
        ++hyp_moves_to_go)
    {
        // Calculate thinking time for hypothetic "moves to go"-value
        int32_t hyp_time = limits.game_clock[c].time
            + limits.game_clock[c].inc * (hyp_moves_to_go - 1)
            - emergency_base_time
            - emergency_move_time * min (hyp_moves_to_go, emergency_move_horizon);

        if (hyp_time < 0) hyp_time = 0;

        uint32_t opt_time = min_thinking_time + remaining_time<OPTIMUM_TIME> (hyp_time, hyp_moves_to_go, current_ply, slow_mover);
        uint32_t max_time = min_thinking_time + remaining_time<MAXIMUM_TIME> (hyp_time, hyp_moves_to_go, current_ply, slow_mover);

        if (_optimum_search_time > opt_time) _optimum_search_time = opt_time;
        if (_maximum_search_time > max_time) _maximum_search_time = max_time;
    }

    if (bool (*(Options["Ponder"]))) _optimum_search_time += _optimum_search_time / 4;

    // Make sure that _optimum_search_time is not over absolute _maximum_search_time
    if (_optimum_search_time > _maximum_search_time)
    {
        _optimum_search_time = _maximum_search_time;
    }
}

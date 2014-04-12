#include "TimeManager.h"

#include <cfloat>
#include <cmath>
#include <algorithm>

#include "UCI.h"

using namespace std;
using namespace Searcher;

namespace {

    enum TimeT { OPTIMUM_TIME, MAXIMUM_TIME };

    const u08    MoveHorizon = 50;    // Plan time management at most this many moves ahead
    const double MaxRatio    = 07.00; // When in trouble, we can step over reserved time with this ratio
    const double StealRatio  = 00.33; // However we must not steal time from remaining moves over this ratio

    const double Scale       = 09.30;
    const double Shift       = 59.80;
    const double SkewFactor  = 00.172;

    // move_importance() is a skew-logistic function based on naive statistical
    // analysis of "how many games are still undecided after n half-moves".
    // Game is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from CCRL game database with some simple filtering criteria.
    inline double move_importance (u16 ply)
    {
        return pow ((1 + exp ((ply - Shift) / Scale)), -SkewFactor) + DBL_MIN; // Ensure non-zero
    }

    // remaining_time() calculate the time remaining
    template<TimeT TT>
    inline u32 remaining_time (u32 time, u08 moves_to_go, u16 game_ply, u16 slow_mover)
    {
        const double TMaxRatio   = (OPTIMUM_TIME == TT ? 1 : MaxRatio);
        const double TStealRatio = (MAXIMUM_TIME == TT ? 0 : StealRatio);

        double  this_moves_importance = (move_importance (game_ply) * slow_mover) / 100;
        double other_moves_importance = 0.0;

        for (u08 i = 1; i < moves_to_go; ++i)
        {
            other_moves_importance += move_importance (game_ply + 2 * i);
        }

        double time_ratio1 = (TMaxRatio * this_moves_importance) / (TMaxRatio * this_moves_importance + other_moves_importance);
        double time_ratio2 = (this_moves_importance + TStealRatio * other_moves_importance) / (this_moves_importance + other_moves_importance);

        return u32 (floor (time * min (time_ratio1, time_ratio2)));
    }

}

void TimeManager::initialize (const LimitsT &limits, u16 game_ply, Color c)
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
    minimum_thinking_time  : No matter what, use at least this much thinking before doing the move
    slow_mover             : Increasing the value make the engine play slow.
    */

    // Read uci parameters
    u08 emergency_move_horizon = i32 (Options["Emergency Move Horizon"]);
    u32 emergency_base_time    = i32 (Options["Emergency Base Time"]);
    u32 emergency_move_time    = i32 (Options["Emergency Move Time"]);
    u32 minimum_thinking_time  = i32 (Options["Minimum Thinking Time"]);
    u16 slow_mover             = i32 (Options["Slow Mover"]);

    // Initialize to maximum values but unstable_pv_extra_time that is reset
    _unstable_pv_factor  = 1.0;
    _optimum_search_time = _maximum_search_time = max (limits.gameclock[c].time, minimum_thinking_time);

    u08 tot_movestogo = (limits.movestogo != 0 ? min (limits.movestogo, MoveHorizon) : MoveHorizon);
    // We calculate optimum time usage for different hypothetic "moves to go"-values and choose the
    // minimum of calculated search time values. Usually the greatest hyp_movestogo gives the minimum values.
    for (u08 hyp_movestogo = 1; hyp_movestogo <= tot_movestogo; ++hyp_movestogo)
    {
        // Calculate thinking time for hypothetic "moves to go"-value
        i32 hyp_time = limits.gameclock[c].time
            + limits.gameclock[c].inc * (hyp_movestogo - 1)
            - emergency_base_time
            - emergency_move_time * min (hyp_movestogo, emergency_move_horizon);

        if (hyp_time < 0) hyp_time = 0;

        u32 opt_time = minimum_thinking_time + remaining_time<OPTIMUM_TIME> (hyp_time, hyp_movestogo, game_ply, slow_mover);
        u32 max_time = minimum_thinking_time + remaining_time<MAXIMUM_TIME> (hyp_time, hyp_movestogo, game_ply, slow_mover);

        if (_optimum_search_time > opt_time) _optimum_search_time = opt_time;
        if (_maximum_search_time > max_time) _maximum_search_time = max_time;
    }

    if (bool (Options["Ponder"]))
    {
        _optimum_search_time += _optimum_search_time / 4;
    }

    // Make sure that _optimum_search_time is not over _maximum_search_time
    if (_optimum_search_time > _maximum_search_time)
    {
        _optimum_search_time = _maximum_search_time;
    }
}

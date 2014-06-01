#include "TimeManager.h"

#include <cfloat>
#include <algorithm>

#include "UCI.h"

using namespace std;
using namespace Searcher;

namespace {

    enum TimeT { OPTIMUM_TIME, MAXIMUM_TIME };

    const double MaxRatio    = 07.00; // When in trouble, can step over reserved time with this ratio
    const double StealRatio  = 00.33; // However must not steal time from remaining moves over this ratio

    const double Scale       = 09.30;
    const double Shift       = 59.80;
    const double SkewFactor  = 00.172;

    const u32 ClockTime    = 60; // Always attempt to keep at least this much time (in ms) at clock
    const u08 MoveHorizon  = 40; // Plan time management at most this many moves ahead
    const u32 MoveTime     = 30; // Attempt to keep at least this much time (in ms) for each remaining move
    const u32 ThinkingTime = 20; // No matter what, use at least this much time (in ms) before doing the move
         u16 SlowMover    = 80; // Increasing the value make the engine play slow.

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
    inline u32 remaining_time (u32 time, u08 moves_to_go, u16 game_ply)
    {
        const double TMaxRatio   = (OPTIMUM_TIME == TT ? 1 : MaxRatio);
        const double TStealRatio = (MAXIMUM_TIME == TT ? 0 : StealRatio);

        double  this_move_imp = (move_importance (game_ply) * SlowMover) / 100;
        double other_move_imp = 0.0;

        for (u08 i = 1; i < moves_to_go; ++i)
        {
            other_move_imp += move_importance (game_ply + 2 * i);
        }

        double time_ratio1 = (TMaxRatio * this_move_imp) / (TMaxRatio * this_move_imp + other_move_imp);
        double time_ratio2 = (this_move_imp + TStealRatio * other_move_imp) / (this_move_imp + other_move_imp);

        return i32 (time * min (time_ratio1, time_ratio2));
    }

}

void TimeManager::initialize (const LimitsT &limits, u16 game_ply, Color c)
{
    // Read uci parameters
    //ClockTime    = i32 (Options["Clock Time"]);
    //MoveHorizon  = i32 (Options["Move Horizon"]);
    //MoveTime     = i32 (Options["Move Time"]);
    //ThinkingTime = i32 (Options["Thinking Time"]);
    SlowMover    = i32 (Options["Slow Mover"]);

    // Initialize unstable pv factor to 1 and search times to maximum values
    _unstable_pv_factor  = 1.0;
    _optimum_time = _maximum_time = max (limits.gameclock[c].time, ThinkingTime);

    u08 tot_movestogo = (limits.movestogo != 0 ? min (limits.movestogo, MoveHorizon) : MoveHorizon);
    // Calculate optimum time usage for different hypothetic "moves to go"-values and choose the
    // minimum of calculated search time values. Usually the greatest hyp_movestogo gives the minimum values.
    for (u08 hyp_movestogo = 1; hyp_movestogo <= tot_movestogo; ++hyp_movestogo)
    {
        // Calculate thinking time for hypothetic "moves to go"-value
        i32 hyp_time =
            + limits.gameclock[c].time
            + limits.gameclock[c].inc * (hyp_movestogo - 1)
            - ClockTime
            - MoveTime * hyp_movestogo;

        if (hyp_time < 0) hyp_time = 0;

        u32 opt_time = ThinkingTime + remaining_time<OPTIMUM_TIME> (hyp_time, hyp_movestogo, game_ply);
        u32 max_time = ThinkingTime + remaining_time<MAXIMUM_TIME> (hyp_time, hyp_movestogo, game_ply);

        if (_optimum_time > opt_time) _optimum_time = opt_time;
        if (_maximum_time > max_time) _maximum_time = max_time;
    }

    if (bool (Options["Ponder"]))
    {
        _optimum_time += _optimum_time / 4;
    }

    // Make sure that _optimum_time is not over _maximum_time
    if (_optimum_time > _maximum_time)
    {
        _optimum_time = _maximum_time;
    }
}

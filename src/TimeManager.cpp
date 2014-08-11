#include "TimeManager.h"

#include <cfloat>

#include "UCI.h"

using namespace std;

namespace {

    enum TimeT { OPTIMUM_TIME, MAXIMUM_TIME };

    const double MaxRatio    = 07.00; // When in trouble, can step over reserved time with this ratio
    const double StealRatio  = 00.33; // However must not steal time from remaining moves over this ratio

    const double Scale       = 09.30;
    const double Shift       = 59.80;
    const double SkewFactor  = 00.172;

    u08 MaxMoveHorizon        = 50; // Plan time management at most this many moves ahead

    u32 EmergencyClockTime    = 60; // Always attempt to keep at least this much time (in ms) at clock
    u08 EmergencyMoveHorizon  = 40; // Be prepared to always play at least this many moves
    u32 EmergencyMoveTime     = 30; // Attempt to keep at least this much time (in ms) for each remaining move
    u32 MinimumThinkingTime   = 20; // No matter what, use at least this much time (in ms) before doing the move
    i32 Slowness              = 80; // Slowliness, in %age.

    // move_importance() is a skew-logistic function based on naive statistical
    // analysis of "how many games are still undecided after 'n' half-moves".
    // Game is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from CCRL game database with some simple filtering criteria.
    inline double move_importance (i32 ply)
    {
        return pow ((1 + exp ((ply - Shift) / Scale)), -SkewFactor) + DBL_MIN; // Ensure non-zero
    }

    template<TimeT TT>
    // remaining_time<>() calculate the time remaining
    inline u32 remaining_time (u32 time, u08 movestogo, i32 game_ply)
    {
        const double TMaxRatio   = OPTIMUM_TIME == TT ? 1 : MaxRatio;
        const double TStealRatio = MAXIMUM_TIME == TT ? 0 : StealRatio;

        double  this_move_imp = move_importance (game_ply) * Slowness / 100;
        double other_move_imp = 0.0;
        for (u08 i = 1; i < movestogo; ++i)
        {
            other_move_imp += move_importance (game_ply + 2 * i);
        }

        double time_ratio1 = (TMaxRatio * this_move_imp) / (TMaxRatio * this_move_imp + other_move_imp);
        double time_ratio2 = (this_move_imp + TStealRatio * other_move_imp) / (this_move_imp + other_move_imp);

        return i32(time * min (time_ratio1, time_ratio2));
    }

}

void TimeManager::initialize (const GameClock &gameclock, u08 movestogo, i32 game_ply)
{
    // Read uci parameters
    //EmergencyClockTime   = i32(Options["Emergency Clock Time"]);
    //EmergencyMoveHorizon = i32(Options["Emergency Move Horizon"]);
    //EmergencyMoveTime    = i32(Options["Emergency Move Time"]);
    //MinimumThinkingTime  = i32(Options["Minimum Thinking Time"]);
    Slowness             = i32(Options["Slowness"]);

    // Initialize unstable pv factor to 1 and search times to maximum values
    _unstable_pv_factor  = 1.0;
    _optimum_time = _maximum_time = max (gameclock.time, MinimumThinkingTime);

    u08 tot_movestogo = movestogo ? min (movestogo, MaxMoveHorizon) : MaxMoveHorizon;
    // Calculate optimum time usage for different hypothetic "moves to go"-values and choose the
    // minimum of calculated search time values. Usually the greatest hyp_movestogo gives the minimum values.
    for (u08 hyp_movestogo = 1; hyp_movestogo <= tot_movestogo; ++hyp_movestogo)
    {
        // Calculate thinking time for hypothetic "moves to go"-value
        i32 hyp_time =
            + gameclock.time
            + gameclock.inc * (hyp_movestogo - 1)
            - EmergencyClockTime
            - EmergencyMoveTime * min (hyp_movestogo, EmergencyMoveHorizon);

        if (hyp_time < 0) hyp_time = 0;

        u32 opt_time = MinimumThinkingTime + remaining_time<OPTIMUM_TIME> (hyp_time, hyp_movestogo, game_ply);
        u32 max_time = MinimumThinkingTime + remaining_time<MAXIMUM_TIME> (hyp_time, hyp_movestogo, game_ply);

        if (_optimum_time > opt_time) _optimum_time = opt_time;
        if (_maximum_time > max_time) _maximum_time = max_time;
    }

    if (bool (Options["Ponder"])) _optimum_time += _optimum_time / 4;

    // Make sure that _optimum_time is not over _maximum_time
    if (_optimum_time > _maximum_time) _optimum_time = _maximum_time;
}


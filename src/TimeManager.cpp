#include "TimeManager.h"

#include <cfloat>

#include "UCI.h"

using namespace std;

namespace {

    enum TimeT { OPTIMUM_TIME, MAXIMUM_TIME };

    const float MaxStepRatio  = 07.00f; // When in trouble, can step over reserved time with this ratio
    const float MaxStealRatio = 00.33f; // However must not steal time from remaining moves over this ratio

    const float Scale       = 09.30f;
    const float Shift       = 59.80f;
    const float SkewFactor  = 00.172f;

    u08 MaximumMoveHorizon    = 50; // Plan time management at most this many moves ahead

    u32 EmergencyClockTime    = 60; // Always attempt to keep at least this much time (in ms) at clock
    u08 EmergencyMoveHorizon  = 40; // Be prepared to always play at least this many moves
    u32 EmergencyMoveTime     = 30; // Attempt to keep at least this much time (in ms) for each remaining move
    u32 MinimumThinkingTime   = 20; // No matter what, use at least this much time (in ms) before doing the move
    i32 Slowness              = 80; // Slowliness, in %age.

    // move_importance() is a skew-logistic function based on naive statistical
    // analysis of "how many games are still undecided after 'n' half-moves".
    // Game is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from CCRL game database with some simple filtering criteria.
    inline float move_importance (i32 game_ply)
    {
        return pow ((1 + exp ((game_ply - Shift) / Scale)), -SkewFactor) + FLT_MIN; // Ensure non-zero
    }

    template<TimeT TT>
    // remaining_time<>() calculate the time remaining
    inline u32 remaining_time (u32 time, u08 movestogo, i32 game_ply)
    {
        const float TStepRatio  = OPTIMUM_TIME == TT ? 1.0f : MaxStepRatio;
        const float TStealRatio = MAXIMUM_TIME == TT ? 0.0f : MaxStealRatio;

        float  this_move_imp = move_importance (game_ply) * Slowness / 0x64; // 100
        float other_move_imp = 0.0f;
        for (u08 i = 1; i < movestogo; ++i)
        {
            other_move_imp += move_importance (game_ply + 2 * i);
        }

        float time_ratio1 = (TStepRatio * this_move_imp) / (TStepRatio * this_move_imp + other_move_imp);
        float time_ratio2 = (this_move_imp + TStealRatio * other_move_imp) / (this_move_imp + other_move_imp);

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

    // Initialize:
    // instability factor to 1.0
    // recapture factor to 1.0
    // and search times to maximum values
    _instability_factor = 1.0f;
    _recapture_factor   = 1.0f;
    _optimum_time = _maximum_time = max (gameclock.time, MinimumThinkingTime);

    u08 tot_movestogo = movestogo ? min (movestogo, MaximumMoveHorizon) : MaximumMoveHorizon;
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

        _optimum_time = min (opt_time, _optimum_time);
        _maximum_time = min (max_time, _maximum_time);
    }

    if (bool(Options["Ponder"])) _optimum_time += _optimum_time / 4;

    // Make sure that _optimum_time is not over _maximum_time
    _optimum_time = min (_maximum_time, _optimum_time);
}

#include "TimeManager.h"

#include <cfloat>
#include <cmath>

TimeManagement::TimeManager TimeMgr; // Global TimeManager

namespace TimeManagement {

    using namespace std;
    using namespace Searcher;
    using namespace UCI;

    namespace {

        enum RemainTimeT { RT_OPTIMUM, RT_MAXIMUM };

        // move_importance() is a skew-logistic function based on naive statistical
        // analysis of "how many games are still undecided after 'n' half-moves".
        // Game is considered "undecided" as long as neither side has >275cp advantage.
        // Data was extracted from CCRL game database with some simple filtering criteria.
        double move_importance (i32 game_ply)
        {
            //                               PLY_SHIFT  PLY_SCALE  SKEW_RATE
            return pow ((1 + exp ((game_ply - 59.800) / 09.300)), -00.172) + DBL_MIN; // Ensure non-zero
        }

        template<RemainTimeT TT>
        // remaining_time<>() calculate the time remaining
        u32 remaining_time (u32 time, u08 movestogo, i32 game_ply)
        {
            // When in trouble, can step over reserved time with this ratio
            const double StepRatio  = RT_OPTIMUM == TT ? 1.0 : 7.00;
            // However must not steal time from remaining moves over this ratio
            const double StealRatio = RT_MAXIMUM == TT ? 0.0 : 0.33;

            double this_move_imp = move_importance (game_ply) * MoveSlowness / 100;
            double that_move_imp = 0.0;
            for (u08 i = 1; i < movestogo; ++i)
            {
                that_move_imp += move_importance (game_ply + 2 * i);
            }

            double time_ratio_1 = (0             + this_move_imp * StepRatio ) / (this_move_imp * StepRatio + that_move_imp);
            double time_ratio_2 = (this_move_imp + that_move_imp * StealRatio) / (this_move_imp * 1         + that_move_imp);

            return u32(time * min (time_ratio_1, time_ratio_2));
        }

    }

    u08  MaximumMoveHorizon  =  50; // Plan time management at most this many moves ahead, in num of moves.
    u08  EmergencyMoveHorizon=  40; // Be prepared to always play at least this many moves, in num of moves.
    u32  EmergencyClockTime  =  60; // Always attempt to keep at least this much time at clock, in milliseconds.
    u32  EmergencyMoveTime   =  30; // Attempt to keep at least this much time for each remaining move, in milliseconds.
    u32  MinimumMoveTime     =  20; // No matter what, use at least this much time before doing the move, in milliseconds.
    i32  MoveSlowness        = 110; // Move Slowness, in %age.
    i32  NodesTime           =   0;
    bool Ponder              = true; // Whether or not the engine should analyze when it is the opponent's turn.

    void TimeManager::initialize (Color c, LimitsT &limits, i32 game_ply, TimePoint now_time)
    {
        // If we have to play in 'nodes as time' mode, then convert from time
        // to nodes, and use resulting values in time management formulas.
        // WARNING: Given npms (nodes per millisecond) must be much lower then
        // real engine speed to avoid time losses.
        if (NodesTime != 0)
        {
            // Only once at game start
            if (available_nodes == 0) available_nodes = NodesTime * limits.game_clock[c].time; // Time is in msec

            // Convert from millisecs to nodes
            limits.game_clock[c].time = i32 (available_nodes);
            limits.game_clock[c].inc *= NodesTime;
            limits.npmsec = NodesTime;
        }

        _start_time = now_time;
        _instability_factor = 1.0;
        
        _optimum_time =
        _maximum_time =
            max (limits.game_clock[c].time, MinimumMoveTime);

        const u08 MaxMovesToGo = limits.movestogo != 0 ? min (limits.movestogo, MaximumMoveHorizon) : MaximumMoveHorizon;
        // Calculate optimum time usage for different hypothetic "moves to go"-values and choose the
        // minimum of calculated search time values. Usually the greatest hyp_movestogo gives the minimum values.
        for (u08 hyp_movestogo = 1; hyp_movestogo <= MaxMovesToGo; ++hyp_movestogo)
        {
            // Calculate thinking time for hypothetic "moves to go"-value
            i32 hyp_time = max (
                + limits.game_clock[c].time
                + limits.game_clock[c].inc * (hyp_movestogo-1)
                - EmergencyClockTime
                - EmergencyMoveTime * min (hyp_movestogo, EmergencyMoveHorizon), 0U);

            u32 opt_time = MinimumMoveTime + remaining_time<RT_OPTIMUM> (hyp_time, hyp_movestogo, game_ply);
            u32 max_time = MinimumMoveTime + remaining_time<RT_MAXIMUM> (hyp_time, hyp_movestogo, game_ply);

            _optimum_time = min (opt_time, _optimum_time);
            _maximum_time = min (max_time, _maximum_time);
        }

        if (Ponder) _optimum_time += _optimum_time / 4;

        // Make sure that _optimum_time is not over _maximum_time
        _optimum_time = min (_maximum_time, _optimum_time);
    }

}

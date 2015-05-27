#include "TimeManager.h"

#include <cfloat>
#include <cmath>

TimeManagement::TimeManager TimeMgr; // Global TimeManager

namespace TimeManagement {

    using namespace std;
    using namespace UCI;

    namespace {

        // move_importance() is a skew-logistic function based on naive statistical
        // analysis of "how many games are still undecided after 'n' half-moves".
        // Game is considered "undecided" as long as neither side has >275cp advantage.
        // Data was extracted from CCRL game database with some simple filtering criteria.
        inline double move_importance (i32 game_ply)
        {
            const double PLY_SCALE = 09.300;
            const double PLY_SHIFT = 59.800;
            const double SKEW_RATE = 00.172;

            return pow ((1 + exp ((game_ply - PLY_SHIFT) / PLY_SCALE)), -SKEW_RATE) + DBL_MIN; // Ensure non-zero
        }

        enum TimeT { TIME_OPTIMUM, TIME_MAXIMUM };

        template<TimeT TT>
        // remaining_time<>() calculate the time remaining
        inline u32 remaining_time (u32 time, u08 movestogo, i32 game_ply)
        {
            const double MAX_STEP_RATIO  = 7.00; // When in trouble, can step over reserved time with this ratio
            const double MAX_STEAL_RATIO = 0.33; // However must not steal time from remaining moves over this ratio

            const double TStepRatio  = TIME_OPTIMUM == TT ? 1.0 : MAX_STEP_RATIO;
            const double TStealRatio = TIME_MAXIMUM == TT ? 0.0 : MAX_STEAL_RATIO;

            double move_imp_0 = move_importance (game_ply) * MoveSlowness / 100;
            double move_imp_1 = 0.0;
            for (u08 i = 1; i < movestogo; ++i)
            {
                move_imp_1 += move_importance (game_ply + 2 * i);
            }

            double time_ratio1 = (TStepRatio * move_imp_0) / (TStepRatio * move_imp_0 + move_imp_1);
            double time_ratio2 = (move_imp_0 + TStealRatio * move_imp_1) / (move_imp_0 + move_imp_1);

            return u32(time * min (time_ratio1, time_ratio2));
        }

    }

    u08  MaximumMoveHorizon  =  50; // Plan time management at most this many moves ahead, in num of moves.
    u08  EmergencyMoveHorizon=  40; // Be prepared to always play at least this many moves, in num of moves.
    u32  EmergencyClockTime  =  60; // Always attempt to keep at least this much time at clock, in milliseconds.
    u32  EmergencyMoveTime   =  30; // Attempt to keep at least this much time for each remaining move, in milliseconds.
    u32  MinimumMoveTime     =  20; // No matter what, use at least this much time before doing the move, in milliseconds.
    i32  MoveSlowness        = 110; // Move Slowness, in %age.
    bool Ponder              = true; // Whether or not the engine should analyze when it is the opponent's turn.

    void TimeManager::initialize (const GameClock &game_clock, u08 movestogo, i32 game_ply)
    {
        //i32 npmsec;
        // If we have to play in 'nodes as time' mode, then convert from time
        // to nodes, and use resulting values in time management formulas.
        // WARNING: Given npms (nodes per millisecond) must be much lower then
        // real engine speed to avoid time losses.
        //if (npmsec)
        //{
        //    if (available_nodes == 0) // Only once at game start
        //        available_nodes = npmsec * game_clock.time; // Time is in msec

        //    // Convert from millisecs to nodes
        //    game_clock.time = (int)available_nodes;
        //    game_clock.inc *= npmsec;
        //    limits.npmsec = npmsec;
        //}


        // Initializes: instability factor and search times to maximum values
        _instability_factor = 1.0;
        _optimum_time =
        _maximum_time =
            max (game_clock.time, MinimumMoveTime);

        movestogo = movestogo != 0 ? min (movestogo, MaximumMoveHorizon) : MaximumMoveHorizon;
        // Calculate optimum time usage for different hypothetic "moves to go"-values and choose the
        // minimum of calculated search time values. Usually the greatest hyp_movestogo gives the minimum values.
        for (u08 hyp_movestogo = 1; hyp_movestogo <= movestogo; ++hyp_movestogo)
        {
            // Calculate thinking time for hypothetic "moves to go"-value
            i32 hyp_time = max (
                + game_clock.time
                + game_clock.inc * (hyp_movestogo-1)
                - EmergencyClockTime
                - EmergencyMoveTime * min (hyp_movestogo, EmergencyMoveHorizon), 0U);

            u32 opt_time = MinimumMoveTime + remaining_time<TIME_OPTIMUM> (hyp_time, hyp_movestogo, game_ply);
            u32 max_time = MinimumMoveTime + remaining_time<TIME_MAXIMUM> (hyp_time, hyp_movestogo, game_ply);

            _optimum_time = min (opt_time, _optimum_time);
            _maximum_time = min (max_time, _maximum_time);
        }

        if (Ponder) _optimum_time += _optimum_time / 4;

        // Make sure that _optimum_time is not over _maximum_time
        _optimum_time = min (_maximum_time, _optimum_time);
    }

}

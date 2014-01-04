#include "TimeManager.h"

#include <algorithm>
#include <cmath>

#include "UCI.h"

using namespace std;

namespace {

    const int32_t MoveHorizon   = 50;    // Plan time management at most this many moves ahead
    const double  MaxRatio      =  7.0;  // When in trouble, we can step over reserved time with this ratio
    const double  StealRatio    =  0.33; // However we must not steal time from remaining moves over this ratio

    const double Scale          =  9.3;
    const double Shift          = 59.8;
    const double SkewFactor     =  0.172;

    //// MoveImportance[] is based on naive statistical analysis of "how many games are still undecided
    //// after n half-moves". Game is considered "undecided" as long as neither side has >275cp advantage.
    //// Data was extracted from CCRL game database with some simple filtering criteria.
    //const int32_t MoveImportance[512] = {
    //    7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780,
    //    7780, 7780, 7780, 7780, 7778, 7778, 7776, 7776, 7776, 7773, 7770, 7768, 7766, 7763, 7757, 7751,
    //    7743, 7735, 7724, 7713, 7696, 7689, 7670, 7656, 7627, 7605, 7571, 7549, 7522, 7493, 7462, 7425,
    //    7385, 7350, 7308, 7272, 7230, 7180, 7139, 7094, 7055, 7010, 6959, 6902, 6841, 6778, 6705, 6651,
    //    6569, 6508, 6435, 6378, 6323, 6253, 6152, 6085, 5995, 5931, 5859, 5794, 5717, 5646, 5544, 5462,
    //    5364, 5282, 5172, 5078, 4988, 4901, 4831, 4764, 4688, 4609, 4536, 4443, 4365, 4293, 4225, 4155,
    //    4085, 4005, 3927, 3844, 3765, 3693, 3634, 3560, 3479, 3404, 3331, 3268, 3207, 3146, 3077, 3011,
    //    2947, 2894, 2828, 2776, 2727, 2676, 2626, 2589, 2538, 2490, 2442, 2394, 2345, 2302, 2243, 2192,
    //    2156, 2115, 2078, 2043, 2004, 1967, 1922, 1893, 1845, 1809, 1772, 1736, 1702, 1674, 1640, 1605,
    //    1566, 1536, 1509, 1479, 1452, 1423, 1388, 1362, 1332, 1304, 1289, 1266, 1250, 1228, 1206, 1180,
    //    1160, 1134, 1118, 1100, 1080, 1068, 1051, 1034, 1012, 1001, 980, 960, 945, 934, 916, 900, 888,
    //    878, 865, 852, 828, 807, 787, 770, 753, 744, 731, 722, 706, 700, 683, 676, 671, 664, 652, 641,
    //    634, 627, 613, 604, 591, 582, 568, 560, 552, 540, 534, 529, 519, 509, 495, 484, 474, 467, 460,
    //    450, 438, 427, 419, 410, 406, 399, 394, 387, 382, 377, 372, 366, 359, 353, 348, 343, 337, 333,
    //    328, 321, 315, 309, 303, 298, 293, 287, 284, 281, 277, 273, 265, 261, 255, 251, 247, 241, 240,
    //    235, 229, 218, 217, 213, 212, 208, 206, 197, 193, 191, 189, 185, 184, 180, 177, 172, 170, 170,
    //    170, 166, 163, 159, 158, 156, 155, 151, 146, 141, 138, 136, 132, 130, 128, 125, 123, 122, 118,
    //    118, 118, 117, 115, 114, 108, 107, 105, 105, 105, 102, 97, 97, 95, 94, 93, 91, 88, 86, 83, 80,
    //    80, 79, 79, 79, 78, 76, 75, 72, 72, 71, 70, 68, 65, 63, 61, 61, 59, 59, 59, 58, 56, 55, 54, 54,
    //    52, 49, 48, 48, 48, 48, 45, 45, 45, 44, 43, 41, 41, 41, 41, 40, 40, 38, 37, 36, 34, 34, 34, 33,
    //    31, 29, 29, 29, 28, 28, 28, 28, 28, 28, 28, 27, 27, 27, 27, 27, 24, 24, 23, 23, 22, 21, 20, 20,
    //    19, 19, 19, 19, 19, 18, 18, 18, 18, 17, 17, 17, 17, 17, 16, 16, 15, 15, 14, 14, 14, 12, 12, 11,
    //    9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    //    8, 8, 8, 8, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    //    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 2, 2, 2, 2,
    //    2, 1, 1, 1, 1, 1, 1, 1 };

    // move_importance() is a skew-logistic function based on naive statistical
    // analysis of "how many games are still undecided after n half-moves".
    // Game is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from CCRL game database with some simple filtering criteria.
    double move_importance (int32_t ply)
    {
        //return MoveImportance[min (ply, 511)];

        return pow ((1 + exp ((ply - Shift) / Scale)), -SkewFactor);
    }

    typedef enum TimeType { OPTIMUM_TIME, MAXIMUM_TIME } TimeType;

    // remaining() calculate the time remaining
    template<TimeType TT>
    int32_t remaining (int32_t time, int32_t moves_to_go, int32_t current_ply, int32_t slow_mover)
    {
        const double TMaxRatio   = (OPTIMUM_TIME == TT ? 1 : MaxRatio);
        const double TStealRatio = (OPTIMUM_TIME == TT ? 0 : StealRatio);

        double  curr_moves_importance = (move_importance (current_ply) * slow_mover) / 100;
        double other_moves_importance = 0.0;

        for (int32_t i = 1; i < moves_to_go; ++i)
        {
            other_moves_importance += move_importance (current_ply + 2 * i);
        }

        double time_ratio1 = (TMaxRatio * curr_moves_importance) / (TMaxRatio * curr_moves_importance + other_moves_importance);
        double time_ratio2 = (curr_moves_importance + TStealRatio * other_moves_importance) / (curr_moves_importance + other_moves_importance);

        return int32_t (floor (time * min (time_ratio1, time_ratio2)));
    }
}

void TimeManager::initialize (const Searcher::Limits &limits, int32_t current_ply, Color c)
{
    /*
    We support four different kind of time controls:

    increment == 0 && moves_to_go == 0 means: x basetime  [sudden death!]
    increment == 0 && moves_to_go != 0 means: x moves in y minutes
    increment >  0 && moves_to_go == 0 means: x basetime + z increment
    increment >  0 && moves_to_go != 0 means: x moves in y minutes + z increment

    Time management is adjusted by following UCI parameters:

    emergencyMoveHorizon : Be prepared to always play at least this many moves
    emergencyBaseTime    : Always attempt to keep at least this much time (in ms) at clock
    emergencyMoveTime    : Plus attempt to keep at least this much time for each remaining emergency move
    minThinkingTime      : No matter what, use at least this much thinking before doing the move
    */

    // Read uci parameters
    int32_t emergency_move_horizon = *(Options["Emergency Move Horizon"]);
    int32_t emergency_base_time    = *(Options["Emergency Base Time"]);
    int32_t emergency_move_time    = *(Options["Emergency Move Time"]);
    int32_t min_thinking_time      = *(Options["Minimum Thinking Time"]);
    int32_t slow_mover             = *(Options["Slow Mover"]);

    // Initialize to maximum values but unstable_pv_extra_time that is reset
    _unstable_pv_extra_time = 0;
    _optimum_search_time    = _maximum_search_time = limits.game_clock[c].time;

    // We calculate optimum time usage for different hypothetic "moves to go"-values and choose the
    // minimum of calculated search time values. Usually the greatest hyp_moves_to_go gives the minimum values.
    for (int32_t hyp_moves_to_go = 1;
        hyp_moves_to_go <= (limits.moves_to_go ? min (int32_t (limits.moves_to_go), MoveHorizon) : MoveHorizon);
        ++hyp_moves_to_go)
    {
        // Calculate thinking time for hypothetic "moves to go"-value
        int32_t hyp_time =  
            limits.game_clock[c].time
            + limits.game_clock[c].inc * (hyp_moves_to_go - 1)
            - emergency_base_time
            - emergency_move_time * min (hyp_moves_to_go, emergency_move_horizon);

        if (hyp_time < 0) hyp_time = 0;

        int32_t opt_time = min_thinking_time + remaining<OPTIMUM_TIME>(hyp_time, hyp_moves_to_go, current_ply, slow_mover);
        int32_t max_time = min_thinking_time + remaining<MAXIMUM_TIME>(hyp_time, hyp_moves_to_go, current_ply, slow_mover);

        _optimum_search_time = min (_optimum_search_time, opt_time);
        _maximum_search_time = min (_maximum_search_time, max_time);
    }

    if (bool (*(Options["Ponder"]))) _optimum_search_time += _optimum_search_time / 4;

    // Make sure that _optimum_search_time is not over absolute _maximum_search_time
    _optimum_search_time = min (_optimum_search_time, _maximum_search_time);
}

#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "Type.h"

i32 main (i32 argc, const char *const *argv)
{
    if (argc != 4)
    {
        printf ("Wrong number of arguments.\n\nUsage:%s <wins> <loss> <draws>\n", argv[0]);
        return EXIT_FAILURE;
    }

    i32 wins  = atoi (argv[1]);
    i32 loss  = atoi (argv[2]);
    i32 draws = atoi (argv[3]);

    i32 total = wins + loss + draws;
    printf ("Total games     : %d\n", total);
    i32 score_diff = wins - loss;
    printf ("Score difference: %d\n", total);
    double score = wins + 0.5*draws;
    printf ("Score           : %g\n", score);
    double win_ratio = wins / total;
    printf ("Win ratio       : %g\n", win_ratio);
    double draw_ratio = draws / total;
    printf ("Draw ratio      : %g\n", draw_ratio);

    double elo_diff = -log (1.0 / win_ratio - 1.0) *400.0 / log (10.0);
    printf ("ELO difference  : %+g\n", elo_diff);
    //double los = .5 + .5 * std::erf ((score_diff) / sqrt (2.0 * (wins+loss)));
    //printf ("LOS             : %g\n", los);

    return EXIT_SUCCESS;
} 
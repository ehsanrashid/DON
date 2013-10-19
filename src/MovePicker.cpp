#include "MovePicker.h"


//// Insertion Sort, guaranteed to be stable, as is needed
//void insertion_sort (MoveScoreList* begin, MoveScoreList* end)
//{
//    MoveScoreList *p, *q;
//
//    for (p = begin + 1; p < end; ++p)
//    {
//        MoveScoreList tmp = *p;
//        for (q = p; q != begin && *(q-1) < tmp; --q)
//        {
//            *q = *(q-1);
//        }
//        *q = tmp;
//    }
//}


void order (ScoredMoveList &lst_sm, bool full)
{
    size_t beg = 0;
    size_t end = lst_sm.size ();

    size_t max = beg;
    for (size_t i = beg; i < end; ++i)
    {
        if (lst_sm[i] > lst_sm[max])
        {
            max = i;
        }
    }

    // list[0-1] =>     sorted
    // list[2-n] => not sorted
    if (full || beg < max)
    {
        std::swap (lst_sm[beg], lst_sm[max]);
        ++beg;

        for (size_t i = beg + 1; i < end; ++i)
        {
            ScoredMove tmp = lst_sm[i];

            max = i - 1;
            if (tmp <= lst_sm[max]) continue;
            while (beg <= max && tmp > lst_sm[max])
            {
                lst_sm[max + 1] = lst_sm[max];
                --max;
            }
            lst_sm[max + 1] = tmp;
        }

    }
}


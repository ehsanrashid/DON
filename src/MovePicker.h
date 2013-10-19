//#pragma once
#ifndef MOVEPICKER_H_
#define MOVEPICKER_H_

#include <vector>
#include <set>
#include "Type.h"


typedef struct ScoredMove
{
    Move    move;
    Score   score;

    friend bool operator<  (const ScoredMove &sm1, const ScoredMove &sm2) { return (sm1.score <  sm2.score); }
    friend bool operator>  (const ScoredMove &sm1, const ScoredMove &sm2) { return (sm1.score >  sm2.score); }
    friend bool operator<= (const ScoredMove &sm1, const ScoredMove &sm2) { return (sm1.score <= sm2.score); }
    friend bool operator>= (const ScoredMove &sm1, const ScoredMove &sm2) { return (sm1.score >= sm2.score); }
    friend bool operator== (const ScoredMove &sm1, const ScoredMove &sm2) { return (sm1.score == sm2.score); }
    friend bool operator!= (const ScoredMove &sm1, const ScoredMove &sm2) { return (sm1.score != sm2.score); }

} ScoredMove;


typedef std::vector<ScoredMove>     ScoredMoveList;
typedef std::set<ScoredMove>        ScoredMoveSet;

extern void order (ScoredMoveList &lst_sm, bool full = true);


template<bool Gain>
// The Stats struct stores moves statistics.
// According to the template parameter the class can store both History and Gains type statistics.
// History records how often different moves have been successful or unsuccessful during the
// current search and is used for reduction and move ordering decisions.
// Gains records the move's best evaluation gain from one ply to the next and is used
// for pruning decisions.
// Entries are stored according only to moving piece and destination square,
// in particular two moves with different origin but same destination and same piece will be considered identical.
struct Stats
{
private:
    Score _Table[PT_NO][SQ_NO];

public:
    static const Score MaxScore = Score (2000);

    //const Score* operator[] (Piece p) const { return &_Table[p][0]; }
    const Score& operator[] (Piece p) const { return  _Table[p][0]; }

    void clear ()
    {
        std::memset (_Table, 0, sizeof (_Table));
    }

    void update (Piece p, Square s, Score v)
    {
        if (false);
        else if (Gain)
        {
            _Table[p][s] = std::max<Score> (v, _Table[p][s] - 1);
        }
        else if (abs (_Table[p][s] + v) < MaxScore)
        {
            _Table[p][s] += v;
        }
    }

};

#endif
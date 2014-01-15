//#pragma once
#ifndef MATERIAL_H_
#define MATERIAL_H_

#include "Type.h"
#include "Endgame.h"

namespace Material {

    // Material::Entry contains various information about a material configuration.
    // It contains a material balance evaluation, a function pointer to a special
    // endgame evaluation function (which in most cases is NULL, meaning that the
    // standard evaluation function will be used), and "scale factors".
    //
    // The scale factors are used to scale the evaluation score up or down.
    // For instance, in KRB vs KR endgames, the score is scaled down by a factor
    // of 4, which will result in scores of absolute value less than one pawn.
    typedef struct Entry
    {
        Key _key;
        int16_t _value;
        uint8_t _factor[CLR_NO];
        Score _space_weight;
        Phase _game_phase;
        EndGame::EndgameBase<Value> *evaluation_func;
        EndGame::EndgameBase<ScaleFactor> *scaling_func[CLR_NO];

        Score material_score () const { return mk_score (_value, _value); }
        Score space_weight ()   const { return _space_weight; }
        Phase game_phase ()     const { return _game_phase; }

        bool specialized_eval_exists ()      const { return evaluation_func != NULL; }
        Value evaluate (const Position &pos) const { return (*evaluation_func) (pos); }

        ScaleFactor scale_factor (const Position &pos, Color c) const;

    } Entry;

    // Entry::scale_factor takes a position and a color as input, and
    // returns a scale factor for the given color. We have to provide the
    // position in addition to the color, because the scale factor need not
    // to be a constant: It can also be a function which should be applied to
    // the position. For instance, in KBP vs K endgames, a scaling function
    // which checks for draws with rook pawns and wrong-colored bishops.
    inline ScaleFactor Entry::scale_factor (const Position &pos, Color c) const
    {
        if (scaling_func[c])
        {
            ScaleFactor sf = (*scaling_func[c]) (pos);
            return (SCALE_FACTOR_NONE == sf) ? ScaleFactor (_factor[c]) : sf;
        }
        return ScaleFactor (_factor[c]);
    }


    typedef HashTable<Entry, 8192> Table;

    Entry* probe (const Position &pos, Table &table, EndGame::Endgames &endgames);
    
    Phase game_phase (const Position &pos);

}

#endif // MATERIAL_H_

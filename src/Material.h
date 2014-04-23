#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _MATERIAL_H_INC_
#define _MATERIAL_H_INC_

#include "Type.h"
#include "Endgame.h"

class Position;

namespace Material {

    // Material::Entry contains various information about a material configuration.
    // It contains a material balance evaluation, a function pointer to a special
    // endgame evaluation function (which in most cases is NULL, meaning that the
    // standard evaluation function will be used), and "scale factors".
    //
    // The scale factors are used to scale the evaluation score up or down.
    // For instance, in KRB vs KR endgames, the score is scaled down by a factor
    // of 4, which will result in scores of absolute value less than one pawn.
    struct Entry
    {

    private:


    public:

        Key     _matl_key;
        i16     _value;
        u08     _factor[CLR_NO];
        Score   _space_weight;
        Phase   _game_phase;

        EndGame::EndgameBase<Value>         *evaluation_func;
        EndGame::EndgameBase<ScaleFactor>   *scaling_func[CLR_NO];

        inline Score material_score () const { return mk_score (_value, _value); }
        inline Score space_weight   () const { return _space_weight; }
        inline Phase game_phase     () const { return _game_phase; }

        inline bool specialized_eval_exists ()      const { return ( evaluation_func != NULL); }
        inline Value evaluate (const Position &pos) const { return (*evaluation_func) (pos); }
        
        template<Color C>
        // Entry::scale_factor() takes a position and a color as input, and
        // returns a scale factor for the given color. We have to provide the
        // position in addition to the color, because the scale factor need not
        // to be a constant: It can also be a function which should be applied to
        // the position. For instance, in KBP vs K endgames, a scaling function
        // which checks for draws with rook pawns and wrong-colored bishops.
        inline ScaleFactor scale_factor (const Position &pos) const
        {
            if (scaling_func[C] != NULL)
            {
                ScaleFactor sf = (*scaling_func[C]) (pos);
                if (SCALE_FACTOR_NONE != sf) return sf;
            }
            return ScaleFactor (_factor[C]);
        }

    };

    typedef HashTable<Entry, 0x2000> Table; // 8192

    Entry* probe     (const Position &pos, Table &table);
    
    Phase game_phase (const Position &pos);

}

#endif // _MATERIAL_H_INC_

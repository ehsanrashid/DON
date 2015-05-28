#ifndef _MATERIAL_H_INC_
#define _MATERIAL_H_INC_

#include "Type.h"
#include "Endgame.h"

class Position;

namespace Material {

    // Material::Entry contains various information about a material configuration.
    // It contains a material imbalance evaluation, a function pointer to a special
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

        Key     matl_key;
        Score   imbalance;
        u08     factor[CLR_NO];
        Phase   game_phase;

        EndGame::EndgameBase<Value>         *evaluation_func;
        EndGame::EndgameBase<ScaleFactor>   *scaling_func[CLR_NO];

        inline bool specialized_eval_exists ()      const { return ( evaluation_func != NULL); }
        inline Value evaluate (const Position &pos) const { return (*evaluation_func) (pos); }
        
        template<Color Own>
        // Entry::scale_factor() takes a position as input, and returns a scale factor for the given color.
        // Have to provide the position in addition to the color, because the scale factor need not to be a constant.
        // It can also be a function which should be applied to the position.
        // For instance, in KBP vs K endgames, a scaling function which checks for draws with rook pawns and wrong-colored bishops.
        inline ScaleFactor scale_factor (const Position &pos) const
        {
            if (scaling_func[Own] != NULL)
            {
                ScaleFactor sf = (*scaling_func[Own]) (pos);
                if (SCALE_FACTOR_NONE != sf) return sf;
            }
            return ScaleFactor (factor[Own]);
        }

    };

    typedef HashTable<Entry, 0x2000> Table; // 8192

    Entry* probe     (const Position &pos);

}

#endif // _MATERIAL_H_INC_

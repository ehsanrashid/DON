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

        bool  specialized_eval_exists ()     const { return   evaluation_func != nullptr; }
        Value evaluate (const Position &pos) const { return (*evaluation_func) (pos); }
        
        // Entry::scale_factor() takes a position and a color as input, and returns a scale factor.
        // Have to provide the position in addition to the color, because the scale factor need not to be a constant.
        // It can also be a function which should be applied to the position.
        // For instance, in KBP vs K endgames, a scaling function which checks for draws with rook pawns and wrong-colored bishops.
        ScaleFactor scale_factor (const Position &pos, Color c) const
        {
            return scaling_func[c] == nullptr || (*scaling_func[c]) (pos) == SCALE_FACTOR_NONE ?
                    ScaleFactor(factor[c]) : (*scaling_func[c]) (pos);
        }

    };

    typedef HashTable<Entry, 0x2000> Table; // 8192

    extern Entry* probe (const Position &pos);

}

#endif // _MATERIAL_H_INC_

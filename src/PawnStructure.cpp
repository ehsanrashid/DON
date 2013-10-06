#include "PawnStructure.h"
#include "BitBoard.h"

using namespace BitBoard;

namespace PawnStructure {

    template<> Bitboard pawns_attacks<WHITE> (Bitboard pawns)
    {
        if (pawns)
        {
            Bitboard w_attacks_east = shift_del<DEL_NE> (pawns /*& ~RANK1BB*/);
            Bitboard w_attacks_west = shift_del<DEL_NW> (pawns /*& ~RANK1BB*/);
            return (w_attacks_east | w_attacks_west);
        }
        return 0;
    }
    template<> Bitboard pawns_attacks<BLACK> (Bitboard pawns)
    {
        if (pawns)
        {
            Bitboard b_attacks_east = shift_del<DEL_SE> (pawns /*& ~RANK8BB*/);
            Bitboard b_attacks_west = shift_del<DEL_SW> (pawns /*& ~RANK8BB*/);
            return (b_attacks_east | b_attacks_west);
        }
        return 0;
    }

    template<> Bitboard pawns_pushable_sgl<WHITE> (Bitboard pawns, Bitboard occ)
    {
        return (pawns) ? shift_del<DEL_S> (~occ) & pawns : 0;
    }
    template<> Bitboard pawns_pushable_sgl<BLACK> (Bitboard pawns, Bitboard occ)
    {
        return (pawns) ? shift_del<DEL_N> (~occ) & pawns : 0;
    }

    template<> Bitboard pawns_pushable_dbl<WHITE> (Bitboard pawns, Bitboard occ)
    {
        if (pawns)
        {
            Bitboard empty   =~occ;
            Bitboard emptyR3 = shift_del<DEL_S> (empty & bb_R4) & empty;
            return shift_del<DEL_S> (emptyR3) & pawns; //pawns_pushable_sgl<WHITE> (pawns, ~emptyR3);
        }
        return 0;
    }
    template<> Bitboard pawns_pushable_dbl<BLACK> (Bitboard pawns, Bitboard occ)
    {
        if (pawns)
        {
            Bitboard empty   =~occ;
            Bitboard emptyR6 = shift_del<DEL_N> (empty & bb_R5) & empty;
            return shift_del<DEL_N> (emptyR6) & pawns; //pawns_pushable_sgl<BLACK> (pawns, ~emptyR6);
        }
        return 0;
    }

    template<> Bitboard pawns_defended<WHITE> (Bitboard pawns)
    {
        return (pawns) ? (pawns & pawns_attacks<WHITE> (pawns)) : 0;
    }
    template<> Bitboard pawns_defended<BLACK> (Bitboard pawns)
    {
        return (pawns) ? (pawns & pawns_attacks<BLACK> (pawns)) : 0;
    }

    template<> Bitboard pawns_defending<WHITE> (Bitboard pawns)
    {
        return (pawns) ? (pawns & pawns_attacks<BLACK> (pawns)) : 0;
    }
    template<> Bitboard pawns_defending<BLACK> (Bitboard pawns)
    {
        return (pawns) ? (pawns & pawns_attacks<WHITE> (pawns)) : 0;
    }

    template<> Bitboard pawns_defended_defending<WHITE> (Bitboard pawns)
    {
        return (pawns) ? (pawns_defended<WHITE> (pawns) & pawns_defending<WHITE> (pawns)) : 0;
    }
    template<> Bitboard pawns_defended_defending<BLACK> (Bitboard pawns)
    {
        return (pawns) ? (pawns_defended<BLACK> (pawns) & pawns_defending<BLACK> (pawns)) : 0;
    }

    template<> Bitboard pawns_defended_not_defending<WHITE> (Bitboard pawns)
    {
        return (pawns) ? (pawns_defended<WHITE> (pawns) & ~pawns_defending<WHITE> (pawns)) : 0;
    }
    template<> Bitboard pawns_defended_not_defending<BLACK> (Bitboard pawns)
    {
        return (pawns) ? (pawns_defended<BLACK> (pawns) & ~pawns_defending<BLACK> (pawns)) : 0;
    }

    template<> Bitboard pawns_defending_not_defended<WHITE> (Bitboard pawns)
    {
        return (pawns) ? (pawns_defending<WHITE> (pawns) & ~pawns_defended<WHITE> (pawns)) : 0;
    }
    template<> Bitboard pawns_defending_not_defended<BLACK> (Bitboard pawns)
    {
        return (pawns) ? (pawns_defending<BLACK> (pawns) & ~pawns_defended<BLACK> (pawns)) : 0;
    }


    template<> Bitboard pawns_attacking<WHITE> (Bitboard pawns, Bitboard pieces)
    {
        return (pawns && pieces) ? (pawns & pawns_attacks<BLACK> (pieces)) : 0;
    }
    template<> Bitboard pawns_attacking<BLACK> (Bitboard pawns, Bitboard pieces)
    {
        return (pawns && pieces) ? (pawns & pawns_attacks<WHITE> (pieces)) : 0;
    }

    template<> Bitboard pawns_rammed<WHITE> (Bitboard wpawns, Bitboard bpawns)
    {
        return (wpawns && bpawns) ? (shift_del<DEL_S> (bpawns) & wpawns) : 0;
    }
    template<> Bitboard pawns_rammed<BLACK> (Bitboard wpawns, Bitboard bpawns)
    {
        return (wpawns && bpawns) ? (shift_del<DEL_N> (bpawns) & wpawns) : 0;
    }



}

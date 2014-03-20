#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _MOVE_GENERATOR_H_INC_
#define _MOVE_GENERATOR_H_INC_

#include "Type.h"

class Position;

const u08   MAX_MOVES   = 255;

typedef struct ValMove
{
    Move    move;
    Value   value;

    // Unary predicate functor used by std::partition to split positive(+ve) scores from
    // remaining ones so to sort separately the two sets, and with the second sort delayed.
    inline bool operator() (const ValMove &vm) { return (vm.value > VALUE_ZERO); }

    inline friend bool operator<  (const ValMove &vm1, const ValMove &vm2) { return (vm1.value <  vm2.value); }
    inline friend bool operator>  (const ValMove &vm1, const ValMove &vm2) { return (vm1.value >  vm2.value); }
    inline friend bool operator<= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value <= vm2.value); }
    inline friend bool operator>= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value >= vm2.value); }
    inline friend bool operator== (const ValMove &vm1, const ValMove &vm2) { return (vm1.value == vm2.value); }
    inline friend bool operator!= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value != vm2.value); }

} ValMove;

namespace MoveGenerator {

    // Types of Generator
    typedef enum GenT : u08
    {
        // PSEUDO-LEGAL MOVES
        RELAX,       // Normal moves.
        EVASION,     // Save the friendly king from check
        CAPTURE,     // Change material balance where an enemy piece is captured.
        QUIET,       // Do not capture pieces but under-promotion is allowed.
        CHECK,       // Checks the enemy King in any way possible.
        QUIET_CHECK, // Do not change material and only checks the enemy King (no capture or promotion).

        // ------------------------
        LEGAL        // Legal moves

    } GenT;

    template<GenT GT>
    extern ValMove* generate (ValMove *moves, const Position &pos);

    // The MoveList struct is a simple wrapper around generate(). It sometimes comes
    // in handy to use this class instead of the low level generate() function.
    template<GenT GT>
    struct MoveList
    {

    private:

        ValMove  moves[MAX_MOVES]
                , *cur
                , *end;

    public:
        explicit MoveList (const Position &pos)
            : cur (moves)
            , end (generate<GT>(moves, pos))
        {
            end->move = MOVE_NONE;
        }

        inline void operator++ () { ++cur; }
        inline void operator-- () { --cur; }
        //inline void begin      () { cur = moves; }
        //inline void endin      () { cur = end-1; }

        inline Move operator* () const { return cur->move; }

        inline u16 size  () const { return end - moves; }

        bool contains (Move m) const
        {
            for (const ValMove *itr = moves; itr != end; ++itr)
            {
                if (itr->move == m) return true;
            }
            return false;
        }

        //template<class charT, class Traits, GenT GT>
        //inline friend std::basic_ostream<charT, Traits>&
        //    operator<< (std::basic_ostream<charT, Traits> &os, MoveList<GT> &movelist)
        //{
        //    ValMove *cur = movelist.cur;
        //    for ( ; *movelist; ++movelist)
        //    {
        //        os << *movelist << std::endl;
        //    }
        //    movelist.cur = cur;
        //    return os;
        //}

    };

}

#endif // _MOVE_GENERATOR_H_INC_

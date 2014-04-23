#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _MOVE_GENERATOR_H_INC_
#define _MOVE_GENERATOR_H_INC_

#include "Type.h"

class Position;

const u08   MAX_MOVES   = 255;

struct ValMove
{
public:
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

};

namespace MoveGenerator {

    // Types of Generator
    enum GenT : u08
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

    };

    template<GenT GT>
    extern ValMove* generate (ValMove *moves, const Position &pos);

    // The MoveList struct is a simple wrapper around generate(). It sometimes comes
    // in handy to use this class instead of the low level generate() function.
    template<GenT GT>
    struct MoveList
    {

    private:
        ValMove  moves[MAX_MOVES]
                , *curr
                , *last;

    public:

        explicit MoveList (const Position &pos)
            : curr (moves)
            , last (generate<GT> (moves, pos))
        {
            last->move = MOVE_NONE;
        }

        inline void operator++ () { ++curr; }
        inline void operator-- () { --curr; }
        //inline void begin      () { curr = moves; }
        //inline void end        () { curr = last-1; }

        inline Move operator* () const { return curr->move; }

        inline u16 size       () const { return last - moves; }

        bool contains (Move m) const
        {
            for (const ValMove *itr = moves; itr != last; ++itr)
            {
                if (itr->move == m) return true;
            }
            return false;
        }

        //template<class charT, class Traits, GenT GT>
        //inline friend std::basic_ostream<charT, Traits>&
        //    operator<< (std::basic_ostream<charT, Traits> &os, MoveList<GT> &movelist)
        //{
        //    ValMove *curr = movelist.curr;
        //    for ( ; *movelist; ++movelist)
        //    {
        //        os << *movelist << std::endl;
        //    }
        //    movelist.curr = curr;
        //    return os;
        //}
    };

}

#endif // _MOVE_GENERATOR_H_INC_

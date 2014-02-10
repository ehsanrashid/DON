//#pragma once
#ifndef MOVE_GENERATOR_H_
#define MOVE_GENERATOR_H_

#include "Type.h"

class Position;

const uint16_t MAX_MOVES    = 256;

typedef struct ValMove
{
    Move    move;
    Value   value;

    // Unary predicate functor used by std::partition to split positive(+ve) scores from
    // remaining ones so to sort separately the two sets, and with the second sort delayed.
    inline bool operator() (const ValMove &vm) { return vm.value > VALUE_ZERO; }

    friend bool operator<  (const ValMove &vm1, const ValMove &vm2) { return (vm1.value <  vm2.value); }
    friend bool operator>  (const ValMove &vm1, const ValMove &vm2) { return (vm1.value >  vm2.value); }
    friend bool operator<= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value <= vm2.value); }
    friend bool operator>= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value >= vm2.value); }
    friend bool operator== (const ValMove &vm1, const ValMove &vm2) { return (vm1.value == vm2.value); }
    friend bool operator!= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value != vm2.value); }

} ValMove;

namespace MoveGenerator {

    // Types of Generator
    typedef enum GenT : uint8_t
    {
        // PSEUDO-LEGAL MOVES
        RELAX,       // Normal moves.
        EVASION,     // Save king in check
        CAPTURE,     // Change material balance where an enemy piece is captured.
        QUIET,       // Do not change material, thus no captures nor promotions.
        CHECK,       // Any way checks the enemy King.
        QUIET_CHECK, // Do not change material and only checks the enemy King.

        // ------------------------
        LEGAL        // Legal moves

    } GenT;

    template<GenT GT>
    extern ValMove* generate (ValMove *m_list, const Position &pos);

    // The MoveList struct is a simple wrapper around generate(). It sometimes comes
    // in handy to use this class instead of the low level generate() function.
    template<GenT GT>
    struct MoveList
    {

    private:

        ValMove m_list[MAX_MOVES];
        ValMove *beg
            ,   *cur
            ,   *end;

    public:
        explicit MoveList (const Position &pos)
            : beg (m_list)
            , cur (m_list)
            , end (generate<GT>(m_list, pos))
        {
            end->move = MOVE_NONE;
        }

        void operator++ () { ++cur; }
        void operator-- () { --cur; }
        void operator!  () { cur = beg; }

        Move operator* () const { return cur->move; }

        size_t size () const { return end - beg; }

        bool contains (Move m) const
        {
            for (const ValMove *itr = beg; itr != end; ++itr)
            {
                if (itr->move == m) return true;
            }
            return false;
        }

        //template<class charT, class Traits, GenT GT>
        //inline friend std::basic_ostream<charT, Traits>&
        //    operator<< (std::basic_ostream<charT, Traits> &os, MoveList<GT> &mov_lst)
        //{
        //    ValMove *cur = mov_lst.cur;
        //    for ( ; *mov_lst; ++mov_lst)
        //    {
        //        os << *mov_lst << std::endl;
        //    }
        //    mov_lst.cur = cur;
        //    return os;
        //}

    };

}

#endif

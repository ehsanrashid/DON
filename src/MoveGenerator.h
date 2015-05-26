#ifndef _MOVE_GENERATOR_H_INC_
#define _MOVE_GENERATOR_H_INC_

#include "Type.h"

class Position;

const u08 MAX_MOVES = 0xFF; // 255

namespace MoveGen {

    struct ValMove
    {
    public:
        Move    move;
        Value   value;

        operator Move () const  { return move; }
        void operator= (Move m) { move = m; }
        
        friend bool operator<  (const ValMove &vm1, const ValMove &vm2) { return vm1.value <  vm2.value; }
        friend bool operator>  (const ValMove &vm1, const ValMove &vm2) { return vm1.value >  vm2.value; }
        friend bool operator<= (const ValMove &vm1, const ValMove &vm2) { return vm1.value <= vm2.value; }
        friend bool operator>= (const ValMove &vm1, const ValMove &vm2) { return vm1.value >= vm2.value; }
        friend bool operator== (const ValMove &vm1, const ValMove &vm2) { return vm1.value == vm2.value; }
        friend bool operator!= (const ValMove &vm1, const ValMove &vm2) { return vm1.value != vm2.value; }

    };

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
    class MoveList
    {

    private:
        ValMove  _moves_beg[MAX_MOVES]
              , *_moves_end;

    public:

        explicit MoveList (const Position &pos)
            : _moves_end (generate<GT> (_moves_beg, pos))
        {
            // Optional Terminator
            *_moves_end = MOVE_NONE;
        }

        inline const ValMove* begin () const { return _moves_beg; }
        inline const ValMove* end   () const { return _moves_end; }
  
        inline size_t size    () const { return size_t(_moves_end - _moves_beg); }
        
        bool contains (Move move) const
        {
            for (const auto &m : *this)
            {
                if (m == move) return true;
            }
            return false;
        }

        //template<class CharT, class Traits, GenT GT>
        //friend std::basic_ostream<CharT, Traits>&
        //    operator<< (std::basic_ostream<CharT, Traits> &os, MoveList<GT> &moveList);
    };

    //template<class CharT, class Traits, GenT GT>
    //inline std::basic_ostream<CharT, Traits>&
    //    operator<< (std::basic_ostream<CharT, Traits> &os, MoveList<GT> &moveList)
    //{
    //    for (const auto &m : moveList)
    //    {
    //        os << m << std::endl;
    //    }
    //    return os;
    //}
}

#endif // _MOVE_GENERATOR_H_INC_

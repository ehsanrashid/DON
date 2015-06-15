#ifndef _ENDGAME_H_INC_
#define _ENDGAME_H_INC_

#include <map>
#include <memory>
#include <type_traits>
#include <utility>

#include "Type.h"

class Position;

namespace EndGame {

    // Endgame Type lists all supported endgames
    enum EndgameT
    {
        // Evaluation functions
        KXK,   // Generic "mate lone king" eval
        KPK,   // KP vs K
        KBNK,  // KBN vs K
        KNNK,  // KNN vs K
        KRKP,  // KR vs KP
        KRKB,  // KR vs KB
        KRKN,  // KR vs KN
        KQKP,  // KQ vs KP
        KQKR,  // KQ vs KR
        KBBKN, // KBB vs KN

        // Scaling functions
        SCALE_FUNS,

        // Generic Scaling functions
        KBPsKs,  // KBPs vs K+s
        KQKRPs,  // KQ vs KR+Ps

        KRPKR,   // KRP vs KR
        KRPKB,   // KRP vs KB
        KRPPKRP, // KRPP vs KRP
        KPsK,    // KPs vs Ks
        KPKP,    // KP vs KP
        KNPK,    // KNP vs K
        KBPKB,   // KBP vs KB
        KBPPKB,  // KBPP vs KB
        KBPKN,   // KBP vs KN
        KNPKB    // KNP vs KB

    };

    // Endgame functions can be of two types depending on whether they return a
    // Value or a ScaleFactor.
    template<EndgameT E>
    using eg_type = typename std::conditional<E < SCALE_FUNS, Value, ScaleFactor>::type;

    // Base and derived templates for endgame evaluation and scaling functions
    template<typename T>
    class EndgameBase
    {
    protected:
        
        Color _strong_side
            ,   _weak_side;

    public:

        explicit EndgameBase (Color c)
            : _strong_side ( c)
            ,   _weak_side (~c)
        {}

        virtual ~EndgameBase () = default;

        Color strong_side () const { return _strong_side; }
        Color   weak_side () const { return   _weak_side; }

        virtual T operator() (const Position &pos) const = 0;

    };

    template<EndgameT E, typename T = eg_type<E>>
    class Endgame
        : public EndgameBase<T>
    {

    public:

        explicit Endgame (Color c)
            : EndgameBase<T> (c)
        {}

        T operator() (const Position &pos) const override;
    };

    // The Endgames class stores the pointers to endgame evaluation and scaling base objects in two std::map. 
    // Uses polymorphism to invoke the actual endgame function by calling its virtual operator().
    class Endgames
    {

    private:
        template<typename T>
        using Map = std::map<Key, std::unique_ptr<EndgameBase<T>>>;

        std::pair<Map<Value>, Map<ScaleFactor>> maps;

        template<typename T>
        Map<T>& map ()
        {
            return std::get<std::is_same<T, ScaleFactor>::value> (maps);
        }

        template<EndgameT E, typename T = eg_type<E>>
        void add (const std::string &code);

    public:

        Endgames ();

        template<typename T>
        EndgameBase<T>* probe (Key matl_key)
        {
            return map<T>().count (matl_key) != 0 ? map<T>()[matl_key].get () : nullptr;
        }
    };

    extern void initialize ();

    extern void exit ();

}

extern EndGame::Endgames *EndGames; // Global Endgames

#endif // _ENDGAME_H_INC_

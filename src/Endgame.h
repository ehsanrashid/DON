//#pragma once
#ifndef ENDGAME_H_
#define ENDGAME_H_

#include <map>
#include "Type.h"
#include "Position.h"

namespace EndGame {

    // EndgameType lists all supported endgames
    typedef enum EndgameType
    {
        // Evaluation functions
        KXK,   // Generic "mate lone king" eval
        KPK,   // KP vs K
        KBNK,  // KBN vs K
        KNNK,  // KNN vs K
        KBPK,  // KBP VS K --- TODO::
        KRKP,  // KR vs KP
        KRKB,  // KR vs KB
        KRKN,  // KR vs KN
        KQKP,  // KQ vs KP
        KQKR,  // KQ vs KR
        KBBKN, // KBB vs KN

        // Scaling functions
        SCALE_FUNS,

        // Generic Scaling functions
        KBPsKs,  // KB+pawns vs K+s
        KQKRPs,  // KQ vs KR+pawns

        KRPKR,   // KRP vs KR
        KRPKB,   // KRP vs KB
        KRPPKRP, // KRPP vs KRP
        KPsK,    // KPs vs Ks
        KPKP,    // KP vs KP
        KNPK,    // KNP vs K
        KBPKB,   // KBP vs KB
        KBPPKB,  // KBPP vs KB
        KBPKN,   // KBP vs KN
        KNPKB   // KNP vs KB

    } EndgameType;


    // Endgame functions can be of two types according if return a Value or a ScaleFactor.
    // Type eg_fun<int32_t>::type equals to either ScaleFactor or Value depending if the template parameter is 0 or 1.
    template<int32_t> struct eg_fun;
    template<> struct eg_fun<0> { typedef Value type; };
    template<> struct eg_fun<1> { typedef ScaleFactor type; };

    // Base and derived templates for endgame evaluation and scaling functions
    template<typename T>
    struct EndgameBase
    {
    public:

        virtual ~EndgameBase () {}

        virtual Color color () const = 0;

        virtual T operator() (const Position &pos) const = 0;

    };

    template<EndgameType E, typename T = typename eg_fun<(E > SCALE_FUNS)>::type>
    struct Endgame
        : public EndgameBase<T>
    {
    private:
        Color _stong_side, _weak_side;

    public:

        explicit Endgame (Color c)
            : _stong_side(c)
            , _weak_side(~c)
        {}

        inline Color color () const { return _stong_side; }

        T operator() (const Position &pos) const;

    };

    // Endgames class stores in two std::map the pointers to endgame evaluation
    // and scaling base objects. Then we use polymorphism to invoke the actual
    // endgame function calling its operator() that is virtual.
    typedef class Endgames
    {

        typedef std::map<Key, EndgameBase<eg_fun<0>::type>*> M1;
        typedef std::map<Key, EndgameBase<eg_fun<1>::type>*> M2;

        M1 m1;
        M2 m2;

        inline M1& map (M1::mapped_type) { return m1; }
        inline M2& map (M2::mapped_type) { return m2; }

        template<EndgameType E>
        void add (const std::string &code);

    public:

        Endgames ();
        ~Endgames ();

        template<class T>
        inline T probe (Key key, T &eg)
        {
            return eg = (map (eg).count (key) ? map (eg)[key] : NULL);
        }

    } Endgames;

}

#endif

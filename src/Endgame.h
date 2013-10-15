//#pragma once
#ifndef ENDGAME_H_
#define ENDGAME_H_

#include <map>
#include "Type.h"
#include "Position.h"

// EndgameType lists all supported endgames
enum EndgameType
{
    // Evaluation functions

    KNNK,  // KNN vs K
    KXK,   // Generic "mate lone king" eval
    KBNK,  // KBN vs K
    KPK,   // KP vs K
    KRKP,  // KR vs KP
    KRKB,  // KR vs KB
    KRKN,  // KR vs KN
    KQKP,  // KQ vs KP
    KQKR,  // KQ vs KR
    KBBKN, // KBB vs KN
    KmmKm, // K and two minors vs K and one or two minors

    // Scaling functions
    SCALE_FUNS,

    KBPsK,   // KB+pawns vs K
    KQKRPs,  // KQ vs KR+pawns
    KRPKR,   // KRP vs KR
    KRPPKRP, // KRPP vs KRP
    KPsK,    // King and pawns vs king
    KBPKB,   // KBP vs KB
    KBPPKB,  // KBPP vs KB
    KBPKN,   // KBP vs KN
    KNPK,    // KNP vs K
    KNPKB,   // KNP vs KB
    KPKP     // KP vs KP
};


// Endgame functions can be of two types according if return a Value or a ScaleFactor.
// Type eg_fun<int>::type equals to either ScaleFactor or Value depending if the template parameter is 0 or 1.
template<int> struct eg_fun;
template<> struct eg_fun<0> { typedef Value type; };
template<> struct eg_fun<1> { typedef ScaleFactor type; };

// Base and derived templates for endgame evaluation and scaling functions
template<typename T>
struct EndgameBase
{
public:

    virtual ~EndgameBase () {}

    virtual Color color () const = 0;

    virtual T operator () (const Position &pos) const = 0;

};

template<EndgameType E, typename T = typename eg_fun<(E > SCALE_FUNS)>::type>
struct Endgame : public EndgameBase<T>
{
private:
    Color _strong_side, _weak_side;

public:

    explicit Endgame(Color c)
        : _strong_side(c)
        , _weak_side(~c)
    {}

    Color color() const { return _strong_side; }

    T operator() (const Position &pos) const;

};


// Endgames class stores in two std::map the pointers to endgame evaluation
// and scaling base objects. Then we use polymorphism to invoke the actual
// endgame function calling its operator() that is virtual.
class Endgames
{

    typedef std::map<Key, EndgameBase<eg_fun<0>::type>*> M1;
    typedef std::map<Key, EndgameBase<eg_fun<1>::type>*> M2;

    M1 m1;
    M2 m2;

    M1& map (M1::mapped_type) { return m1; }
    M2& map (M2::mapped_type) { return m2; }

    template<EndgameType E>
    void add (const std::string &code);

public:

    Endgames ();
    ~Endgames ();

    template<typename T>
    T probe (Key key, T &eg) { return eg = map (eg).count (key) ? map (eg)[key] : NULL; }

};

#endif

#pragma once

#include "Position.h"
#include "PRNG.h"
#include "Type.h"

/// Polyglot::Entry needs 16 bytes to be stored.
///  - Key       8 bytes
///  - Move      2 bytes
///  - Weight    2 bytes
///  - Learn     4 bytes
struct PolyEntry
{
    u64 key;
    u16 move;
    u16 weight;
    u32 learn;

    PolyEntry(u64 k, u16 m, u16 w, u32 l)
        : key(k)
        , move(m)
        , weight(w)
        , learn(l)
    {}
    PolyEntry()
        : PolyEntry(0, 0, 0, 0)
    {}

    PolyEntry& operator=(const PolyEntry&) = default;

    explicit operator Move() const { return Move(move); }

    bool operator==(const PolyEntry &pe) const
    {
        return key == pe.key
            && move == pe.move
            && weight == pe.weight;
    }
    bool operator!=(const PolyEntry &pe) const
    {
        return key != pe.key
            || move != pe.move
            || weight != pe.weight;
    }
    bool operator>(const PolyEntry &pe) const
    {
        return key != pe.key ?
                    key > pe.key :
                    weight != pe.weight ?
                        weight > pe.weight :
                        move > pe.move;
    }
    bool operator<(const PolyEntry &pe) const
    {
        return key != pe.key ?
                    key < pe.key :
                    weight != pe.weight ?
                        weight < pe.weight :
                        move < pe.move;
    }
    bool operator>=(const PolyEntry &pe) const
    {
        return key != pe.key ?
                    key >= pe.key :
                    weight != pe.weight ?
                        weight >= pe.weight :
                        move >= pe.move;
    }
    bool operator<=(const PolyEntry &pe) const
    {
        return key != pe.key ?
                    key <= pe.key :
                    weight != pe.weight ?
                        weight <= pe.weight :
                        move <= pe.move;
    }

    bool operator==(Move m) const { return move == m; }
    bool operator!=(Move m) const { return move != m; }

    explicit operator std::string() const;

};

static_assert (sizeof (PolyEntry) == 16, "Entry size incorrect");

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, const PolyEntry &pe)
{
    os << std::string(pe);
    return os;
}

class PolyBook
{
private:
    static PRNG prng;

    PolyEntry   *entries;
    size_t      entryCount;


    bool        doProbe;
    Bitboard    prevPieces;
    i32         prevPieceCount;
    u08         failCount;

    void clear();

    i64 findIndex(Key) const;
    //i64 findIndex(const Position&) const;
    //i64 findIndex(const std::string&, bool = false) const;

    bool canProbe(const Position&);

public:

    size_t const HeaderSize = 0;

    bool enabled;
    std::string bookFn;

    PolyBook();
    virtual ~PolyBook();

    void initialize(const std::string&);

    Move probe(Position&, i16, bool);

    std::string show(const Position&) const;
};

// Global Polyglot Book
extern PolyBook Book;

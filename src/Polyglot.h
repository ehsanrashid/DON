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

    PolyEntry() = default;
    PolyEntry(u64, u16, u16, u32);

    PolyEntry& operator=(const PolyEntry&) = default;

    explicit operator Move() const { return Move(move); }

    bool operator==(const PolyEntry&) const;
    bool operator!=(const PolyEntry&) const;

    bool operator>(const PolyEntry&) const;
    bool operator<(const PolyEntry&) const;
    
    bool operator>=(const PolyEntry&) const;
    bool operator<=(const PolyEntry&) const;

    bool operator==(Move m) const;
    bool operator!=(Move m) const;

    explicit operator std::string() const;
};

static_assert (sizeof (PolyEntry) == 16, "Entry size incorrect");

extern std::ostream& operator<<(std::ostream&, const PolyEntry&);

class PolyBook
{
private:

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

#pragma once

#include "Position.h"
#include "PRNG.h"
#include "Type.h"

/// Polyglot::Entry needs 16 bytes to be stored.
///  - Key       8 bytes
///  - Move      2 bytes
///  - Weight    2 bytes
///  - Learn     4 bytes
struct PolyEntry {

    u64 key;
    u16 move;
    u16 weight;
    u32 learn;

    explicit operator Move() const { return Move(move); }

    bool operator==(PolyEntry const&) const;
    bool operator!=(PolyEntry const&) const;

    bool operator>(PolyEntry const&) const;
    bool operator<(PolyEntry const&) const;

    bool operator>=(PolyEntry const&) const;
    bool operator<=(PolyEntry const&) const;

    bool operator==(Move) const;
    bool operator!=(Move) const;

    std::string toString() const;
};

static_assert (sizeof (PolyEntry) == 16, "Entry size incorrect");

extern std::ostream& operator<<(std::ostream&, PolyEntry const&);

class PolyBook {

private:

    PolyEntry  *_entryTable{ nullptr };
    u64         _entryCount{ 0 };

    std::string _fnBook;

    bool        _doProbe{ true };
    Bitboard    _pieces{ 0 };
    i32         _pieceCount{ 0 };
    u08         _failCount{ 0 };

    void clear();

    i64 findIndex(Key) const;
    //i64 findIndex(Position const&) const;
    //i64 findIndex(std::string const&) const;

    bool canProbe(Position const&);

public:

    u64 const HeaderSize = 0;

    bool enabled{ false };

    ~PolyBook();

    void initialize(std::string const&);

    Move probe(Position&, i16, bool);

    std::string show(Position const&) const;
};

// Global Polyglot Book
extern PolyBook Book;

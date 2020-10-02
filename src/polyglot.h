#pragma once

#include <string_view>
#include "position.h"
#include "PRNG.h"
#include "type.h"

/// Polyglot::Entry needs 16 bytes to be stored.
///  - Key       8 bytes
///  - Move      2 bytes
///  - Weight    2 bytes
///  - Learn     4 bytes
struct PolyEntry {

    explicit operator Move() const noexcept { return Move(move); }

    bool operator==(PolyEntry const&) const noexcept;
    bool operator!=(PolyEntry const&) const noexcept;

    bool operator>(PolyEntry const&) const noexcept;
    bool operator<(PolyEntry const&) const noexcept;

    bool operator>=(PolyEntry const&) const noexcept;
    bool operator<=(PolyEntry const&) const noexcept;

    bool operator==(Move) const noexcept;
    bool operator!=(Move) const noexcept;

    std::string toString() const;

    u64 key;
    u16 move;
    u16 weight;
    u32 learn;
};

static_assert (sizeof (PolyEntry) == 16, "Entry size incorrect");

extern std::ostream& operator<<(std::ostream&, PolyEntry const&);

class PolyBook {

public:
    PolyBook() = default;
    ~PolyBook();

    void initialize(std::string_view);

    Move probe(Position&, i16, bool);

    std::string show(Position const&) const;

    u64 const HeaderSize = 0;

    bool enabled{ false };

private:
    void clear() noexcept;

    i64 findIndex(Key) const noexcept;
    //i64 findIndex(Position const&) const noexcept;
    //i64 findIndex(std::string_view) const noexcept;

    bool canProbe(Position const&) noexcept;

    PolyEntry *entry{ nullptr };
    u64 entryCount{ 0 };

    std::string filename;

    bool doProbe{ true };

    Bitboard pieces{ 0 };
    i32 pieceCount{ 0 };

    u08 failCount{ 0 };
};

// Global Polyglot Book
extern PolyBook Book;

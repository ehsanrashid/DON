#pragma once

#include <string_view>

#include "position.h"
#include "type.h"
#include "helper/prng.h"

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

    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
};

static_assert (sizeof (PolyEntry) == 16, "Entry size incorrect");

extern std::ostream& operator<<(std::ostream&, PolyEntry const&);

class PolyBook {

public:
    PolyBook() = default;
    ~PolyBook();

    void initialize(std::string_view);

    Move probe(Position&, int16_t, bool);

    std::string show(Position const&) const;

    uint64_t const HeaderSize = 0;

    bool enabled{ false };

private:
    void clear() noexcept;

    int64_t findIndex(Key) const noexcept;
    //int64_t findIndex(Position const&) const noexcept;
    //int64_t findIndex(std::string_view) const noexcept;

    bool canProbe(Position const&) noexcept;

    PolyEntry *entry{ nullptr };
    uint64_t entryCount{ 0 };

    std::string filename;

    bool doProbe{ true };

    Bitboard pieces{ 0 };
    int32_t pieceCount{ 0 };

    uint8_t failCount{ 0 };
};

// Global Polyglot Book
extern PolyBook Book;

#pragma once

#include <string_view>

#include "position.h"
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

    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
};

static_assert (sizeof (PolyEntry) == 16, "Entry size incorrect");

extern std::ostream& operator<<(std::ostream&, PolyEntry const&);

class PolyBook {

public:
    constexpr PolyBook() noexcept;
    ~PolyBook() noexcept;

    void initialize(std::string_view);

    Move probe(Position&, int16_t, bool);

    std::string show(Position const&) const;

    static constexpr uint64_t HeaderSize{ 0 * sizeof (PolyEntry) };

    bool enabled;

private:
    void clear() noexcept;

    int64_t findIndex(Key) const noexcept;
    //int64_t findIndex(Position const&) const noexcept;
    //int64_t findIndex(std::string_view) const noexcept;

    bool canProbe(Position const&) noexcept;

    PolyEntry *entryTable;
    uint64_t   entryCount;

    // Last probe info
    Bitboard pieces;
    uint8_t  failCount;
};

// Global Polyglot Book
extern PolyBook Book;

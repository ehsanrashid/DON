/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "prng.h"

#include <algorithm>  // generate(), all_of()

namespace DON {

// ------------------

SplitMix64::SplitMix64(const u64 seed) noexcept :
    state(seed) {}

u64 SplitMix64::next() noexcept {
    update();

    return mix();
}

void SplitMix64::update() noexcept {
    state += u64{0x9E3779B97F4A7C15};  // Derived from the Golden Ratio
}

u64 SplitMix64::mix() const noexcept {
    u64 tmp = state;
    tmp     = (tmp ^ (tmp >> 30)) * u64{0xBF58476D1CE4E5B9};
    tmp     = (tmp ^ (tmp >> 27)) * u64{0x94D049BB133111EB};
    tmp     = (tmp ^ (tmp >> 31));

    return tmp;
}

// ------------------

Xorshift64s::Xorshift64s(const u64 seed) noexcept {
    SplitMix64 seeder(seed);

    // Initialize the state with a mixed SplitMix64 output.
    std::generate(state.begin(), state.end(),
                  [&seeder]() noexcept -> u64 { return seeder.next(); });

    // The all-zero state is not valid for xorshift64*.
    // If SplitMix64 produces zero for state,
    // fall back to a non-zero state.
    if (UNLIKELY(std::all_of(state.begin(), state.end(),
                             [](const u64 s) noexcept -> bool { return s == 0; })))
        state = DefaultState;
}

void Xorshift64s::jump() noexcept {
    constexpr State Jump = {u64{0xDD97D02513476FA5}};

    State tmpState = {0};

    for (const u64 jump : Jump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((jump & bit(b)) != 0)
            {
                tmpState[0] ^= state[0];
            }

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xorshift64s::long_jump() noexcept {
    constexpr State LongJump = {u64{0xAE82CA9F848EBC6D}};

    State tmpState = {0};

    for (const u64 longJump : LongJump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((longJump & bit(b)) != 0)
            {
                tmpState[0] ^= state[0];
            }

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xorshift64s::update() noexcept {
    state[0] ^= state[0] >> 12;
    state[0] ^= state[0] << 25;
    state[0] ^= state[0] >> 27;
}

u64 Xorshift64s::mix() const noexcept { return u64{0x2545F4914F6CDD1D} * state[0]; }

u64 Xorshift64s::rand64() noexcept {
    update();

    return mix();
}

// ------------------

Xoroshiro128ss::Xoroshiro128ss(const u64 seed) noexcept {
    SplitMix64 seeder(seed);

    // Initialize the state with two SplitMix64 outputs.
    std::generate(state.begin(), state.end(),
                  [&seeder]() noexcept -> u64 { return seeder.next(); });

    // The all-zero state is not valid for xoroshiro128**.
    // If SplitMix64 produces zero for both state words,
    // fall back to a non-zero state.
    if (UNLIKELY(std::all_of(state.begin(), state.end(),
                             [](const u64 s) noexcept -> bool { return s == 0; })))
        state = DefaultState;
}

void Xoroshiro128ss::jump() noexcept {
    constexpr State Jump = {u64{0xDF900294D8F554A5}, u64{0x170865DF4B3201FC}};

    State tmpState = {0, 0};

    for (const u64 jump : Jump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((jump & bit(b)) != 0)
            {
                tmpState[0] ^= state[0];
                tmpState[1] ^= state[1];
            }

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xoroshiro128ss::long_jump() noexcept {
    constexpr State LongJump = {u64{0xD2A98B26625EEE7B}, u64{0xDDDF9B1090AA7AC1}};

    State tmpState = {0, 0};

    for (const u64 longJump : LongJump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((longJump & bit(b)) != 0)
            {
                tmpState[0] ^= state[0];
                tmpState[1] ^= state[1];
            }

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xoroshiro128ss::update() noexcept {
    state[1] ^= state[0];

    state[0] = rotl(state[0], 24) ^ state[1] ^ (state[1] << 16);
    state[1] = rotl(state[1], 37);
}

u64 Xoroshiro128ss::mix() const noexcept { return rotl(state[0] * 5, 7) * 9; }

u64 Xoroshiro128ss::rand64() noexcept {
    // Generate the output before advancing the state.
    const u64 rand = mix();

    update();

    return rand;
}

// ------------------

Xoshiro256ss::Xoshiro256ss(const u64 seed) noexcept {
    SplitMix64 seeder(seed);

    // Initialize the state with four SplitMix64 outputs.
    std::generate(state.begin(), state.end(),
                  [&seeder]() noexcept -> u64 { return seeder.next(); });

    // The all-zero state is not valid for xoshiro256**.
    // If SplitMix64 produces zero for all state words,
    // fall back to a non-zero state.
    if (UNLIKELY(std::all_of(state.begin(), state.end(),
                             [](const u64 s) noexcept -> bool { return s == 0; })))
        state = DefaultState;
}

void Xoshiro256ss::jump() noexcept {
    constexpr State Jump = {u64{0x180EC6D33CFD0ABA}, u64{0xD5A61266F0C9392C},
                            u64{0xA9582618E03FC9AA}, u64{0x39ABDC4529B1661C}};

    State tmpState = {0, 0, 0, 0};

    for (const u64 jump : Jump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((jump & bit(b)) != 0)
            {
                tmpState[0] ^= state[0];
                tmpState[1] ^= state[1];
                tmpState[2] ^= state[2];
                tmpState[3] ^= state[3];
            }

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xoshiro256ss::long_jump() noexcept {
    constexpr State LongJump = {u64{0x76E15D3EFEFDCBBF}, u64{0xC5004E441C522FB3},
                                u64{0x77710069854EE241}, u64{0x39109BB02ACBE635}};

    State tmpState = {0, 0, 0, 0};

    for (const u64 longJump : LongJump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((longJump & bit(b)) != 0)
            {
                tmpState[0] ^= state[0];
                tmpState[1] ^= state[1];
                tmpState[2] ^= state[2];
                tmpState[3] ^= state[3];
            }

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xoshiro256ss::update() noexcept {
    const u64 tmp = state[1] << 17;

    state[2] ^= state[0];
    state[3] ^= state[1];
    state[1] ^= state[2];
    state[0] ^= state[3];

    state[2] ^= tmp;

    state[3] = rotl(state[3], 45);
}

u64 Xoshiro256ss::mix() const noexcept { return rotl(state[1] * 5, 7) * 9; }

u64 Xoshiro256ss::rand64() noexcept {
    // Generate the output before advancing the state.
    const u64 rand = mix();

    update();

    return rand;
}

// ------------------

}  // namespace DON

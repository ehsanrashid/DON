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

#include "../utility/prng.h"

#include <algorithm>  // generate<>(), all_of<>()

#include "../utility/attributes.h"
#include "../utility/types.h"

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
    SplitMix64 seeder{seed};

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
    // Polynomial for advancing the (12, 25, 27) recurrence by 2^32 steps.
    constexpr State Jump = {u64{0xBBD5E1C3A495E3E0}};

    State tmpState = {0};

    for (const u64 jump : Jump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((jump & bit(b)) != 0)
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xorshift64s::long_jump() noexcept {
    // Polynomial for advancing the (12, 25, 27) recurrence by 2^48 steps.
    constexpr State LongJump = {u64{0x76C6208C83EE6437}};

    State tmpState = {0};

    for (const u64 longJump : LongJump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((longJump & bit(b)) != 0)
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

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
    SplitMix64 seeder{seed};

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
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

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
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

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
    SplitMix64 seeder{seed};

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
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

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
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

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

Xoshiro512ss::Xoshiro512ss(const u64 seed) noexcept {
    SplitMix64 seeder{seed};

    std::generate(state.begin(), state.end(),
                  [&seeder]() noexcept -> u64 { return seeder.next(); });

    if (UNLIKELY(std::all_of(state.begin(), state.end(),
                             [](const u64 s) noexcept -> bool { return s == 0; })))
        state = DefaultState;
}

void Xoshiro512ss::jump() noexcept {
    constexpr State Jump = {u64{0x33ED89B6E7A353F9}, u64{0x760083D7955323BE},
                            u64{0x2837F2FBB5F22FAE}, u64{0x4B8C5674D309511C},
                            u64{0xB11AC47A7BA28C25}, u64{0xF1BE7667092BCC1C},
                            u64{0x53851EFDB6DF0AAF}, u64{0x1EBBC8B23EAF25DB}};

    State tmpState = {0, 0, 0, 0, 0, 0, 0, 0};

    for (const u64 jump : Jump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((jump & bit(b)) != 0)
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xoshiro512ss::long_jump() noexcept {
    constexpr State LongJump = {u64{0x11467FEF8F921D28}, u64{0xA2A819F2E79C8EA8},
                                u64{0xA8299FC284B3959A}, u64{0xB4D347340CA63EE1},
                                u64{0x1CB0940BEDBFF6CE}, u64{0xD956C5C4FA1F8E17},
                                u64{0x915E38FD4EDA93BC}, u64{0x5B3CCDFA5D7DACA5}};

    State tmpState = {0, 0, 0, 0, 0, 0, 0, 0};

    for (const u64 longJump : LongJump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((longJump & bit(b)) != 0)
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[i];

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    state = tmpState;
}

void Xoshiro512ss::update() noexcept {
    const u64 tmp = state[1] << 11;
    state[2] ^= state[0];
    state[5] ^= state[1];
    state[1] ^= state[2];
    state[7] ^= state[3];
    state[3] ^= state[4];
    state[4] ^= state[5];
    state[0] ^= state[6];
    state[6] ^= state[7];
    state[6] ^= tmp;
    state[7] = rotl(state[7], 21);
}

u64 Xoshiro512ss::mix() const noexcept { return rotl(state[1] * 5, 7) * 9; }

u64 Xoshiro512ss::rand64() noexcept {
    // Generate the output before advancing the state.
    const u64 rand = mix();

    update();

    return rand;
}

// ------------------

Xoshiro1024ss::Xoshiro1024ss(u64 seed) noexcept {
    SplitMix64 seeder{seed};

    std::generate(state.begin(), state.end(),
                  [&seeder]() noexcept -> u64 { return seeder.next(); });

    if (UNLIKELY(std::all_of(state.begin(), state.end(),
                             [](const u64 s) noexcept -> bool { return s == 0; })))
        state = DefaultState;
}

void Xoshiro1024ss::jump() noexcept {
    constexpr State Jump = {u64{0x931197d8e3177f17}, u64{0xb59422e0b9138c5f},  //
                            u64{0xf06a6afb49d668bb}, u64{0xacb8a6412c8a1401},  //
                            u64{0x12304ec85f0b3468}, u64{0xb7dfe7079209891e},  //
                            u64{0x405b7eec77d9eb14}, u64{0x34ead68280c44e4a},  //
                            u64{0xe0e4ba3e0ac9e366}, u64{0x8f46eda8348905b7},  //
                            u64{0x328bf4dbad90d6ff}, u64{0xc8fd6fb31c9effc3},  //
                            u64{0xe899d452d4b67652}, u64{0x45f387286ade3205},  //
                            u64{0x03864f454a8920bd}, u64{0xa68fa28725b1b384}};

    State tmpState = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    for (const u64 jump : Jump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((jump & bit(b)) != 0)
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[(p + i) & StateMask];

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    for (usize i = 0; i < StateSize; ++i)
        state[(p + i) & StateMask] = tmpState[i];
}

void Xoshiro1024ss::long_jump() noexcept {
    constexpr State LongJump = {u64{0x7374156360bbf00f}, u64{0x4630c2efa3b3c1f6},  //
                                u64{0x6654183a892786b1}, u64{0x94f7bfcbfb0f1661},  //
                                u64{0x27d8243d3d13eb2d}, u64{0x9701730f3dfb300f},  //
                                u64{0x2f293baae6f604ad}, u64{0xa661831cb60cd8b6},  //
                                u64{0x68280c77d9fe008c}, u64{0x50554160f5ba9459},  //
                                u64{0x2fc20b17ec7b2a9a}, u64{0x49189bbdc8ec9f8f},  //
                                u64{0x92a65bca41852cc1}, u64{0xf46820dd0509c12a},  //
                                u64{0x52b00c35fbf92185}, u64{0x1e5b3b7f589e03c1}};

    State tmpState = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    for (const u64 longJump : LongJump)
    {
        for (u8 b = 0; b < 64; ++b)
        {
            if ((longJump & bit(b)) != 0)
                for (usize i = 0; i < StateSize; ++i)
                    tmpState[i] ^= state[(p + i) & StateMask];

            // Advance the state through the underlying linear recurrence.
            update();
        }
    }

    for (usize i = 0; i < StateSize; ++i)
        state[(p + i) & StateMask] = tmpState[i];
}

void Xoshiro1024ss::update() noexcept {
    const usize q = p;
    p             = (p + 1) & StateMask;
    state[q] ^= state[p];
    state[q] = rotl(state[p], 25) ^ state[q] ^ (state[q] << 27);
    state[p] = rotl(state[q], 36);
}

u64 Xoshiro1024ss::mix() const noexcept { return rotl(state[(p + 1) & StateMask] * 5, 7) * 9; }

u64 Xoshiro1024ss::rand64() noexcept {
    // Generate the output before advancing the state.
    const u64 rand = mix();

    update();

    return rand;
}

// ------------------

}  // namespace DON

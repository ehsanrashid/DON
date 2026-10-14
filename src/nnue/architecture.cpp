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

// Definition of network structure used in NNUE evaluation function

#include "../nnue/architecture.h"  // IWYU pragma: associated

#include "../misc.h"

// IWYU pragma: no_include "nnue/layers/affine_transform.h"
// IWYU pragma: no_include "nnue/layers/clipped_relu.h"
// IWYU pragma: no_include "nnue/layers/sparse_affine_transform.h"
// IWYU pragma: no_include "nnue/layers/sqr_clipped_relu.h"
// IWYU pragma: no_include "nnue/types.h"
// IWYU pragma: no_include "utility/types.h"

namespace DON::NNUE {

u32 NetworkArchitecture::hash() noexcept {
    // input slice hash
    u32 h = u32{0xEC42E90D};
    h ^= 2 * TransformedFeatureDimensions;

    h = decltype(fc0)::hash(h);
    // TODO: consider including hash value of sqr_ac0 in the overall hash value.
    h = decltype(ac0)::hash(h);
    h = decltype(fc1)::hash(h);
    h = decltype(ac1)::hash(h);
    h = decltype(fc2)::hash(h);
    return h;
}

usize NetworkArchitecture::content_hash() const noexcept {
    usize h = 0;
    combine_hash(h, fc0.content_hash());
    combine_hash(h, sqr_ac0.content_hash());
    combine_hash(h, ac0.content_hash());
    combine_hash(h, fc1.content_hash());
    // TODO: hash_combine()
    // hash_combine(h, sqr_ac1.get_content_hash());
    combine_hash(h, ac1.content_hash());
    combine_hash(h, fc2.content_hash());
    combine_hash(h, hash());
    return h;
}

bool NetworkArchitecture::read_parameters(std::istream& is) noexcept {
    return fc0.read_parameters(is)  //
        && ac0.read_parameters(is)  //
        && fc1.read_parameters(is)  //
        && ac1.read_parameters(is)  //
        && fc2.read_parameters(is);
}

bool NetworkArchitecture::write_parameters(std::ostream& os) const noexcept {
    return fc0.write_parameters(os)  //
        && ac0.write_parameters(os)  //
        && fc1.write_parameters(os)  //
        && ac1.write_parameters(os)  //
        && fc2.write_parameters(os);
}

namespace {

constexpr i64 MULTIPLIER = 600 * OUTPUT_SCALE;
constexpr i64 DIVISOR    = (i64{1} << WEIGHT_SCALE_BITS) * HIDDEN_ONE * 2;

}  // namespace

i32 NetworkArchitecture::propagate(
  const Array<TransformedFeature, TransformedFeatureDimensions>& transformedFeatures,
  const NNZ<L1>&                                                 nnz) const noexcept {

    Buffer buffer;

    fc0.propagate(transformedFeatures.data(), buffer.fc_0_out.data(), nnz);
#if defined(USE_PAIR_ACTIVATIONS)
    sqr_ac0.propagate_pair(buffer.fc_0_out.data(), buffer.concat_buffer.data(),
                           buffer.concat_buffer.data() + FC0Outputs);
#else
    sqr_ac0.propagate(buffer.fc_0_out.data(), buffer.concat_buffer.data());
    ac0.propagate(buffer.fc_0_out.data(), buffer.concat_buffer.data() + FC0Outputs);
#endif
    fc1.propagate(buffer.concat_buffer.data(), buffer.fc_1_out.data());
#if defined(USE_PAIR_ACTIVATIONS)
    sqr_ac1.propagate_pair(buffer.fc_1_out.data(), buffer.concat_buffer.data() + FC0Outputs * 2,
                           buffer.concat_buffer.data() + FC0Outputs * 2 + FC1Outputs);
#else
    sqr_ac1.propagate(buffer.fc_1_out.data(), buffer.concat_buffer.data() + FC0Outputs * 2);
    ac1.propagate(buffer.fc_1_out.data(),
                  buffer.concat_buffer.data() + FC0Outputs * 2 + FC1Outputs);
#endif
    fc2.propagate(buffer.concat_buffer.data(), buffer.fc_2_out.data());

    static_assert(FC0Outputs >= 2);
    i32 fwdOut = buffer.fc_2_out[0];
    i32 skip_0 = buffer.fc_0_out[FC0Outputs - 2] - buffer.fc_0_out[FC0Outputs - 1];
    fwdOut += skip_0;

    // fwdOut is such that 1.0 is equal to (1 << WEIGHT_SCALE_BITS) * HIDDEN_ONE * 2
    // in quantized form, but want 1.0 to be equal to 600 * OUTPUT_SCALE
    // to make overflow impossible cast to int64_t.
    return static_cast<i32>((static_cast<i64>(fwdOut) * MULTIPLIER) / DIVISOR);
}

}  // namespace DON::NNUE

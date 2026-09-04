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

#ifndef NNUE_SIMD_H_INCLUDED
#define NNUE_SIMD_H_INCLUDED

#if defined(USE_SSE2)
    #if defined(USE_AVX2)
        #include <immintrin.h>
    #elif defined(USE_SSE41)
        #include <smmintrin.h>
    #elif defined(USE_SSSE3)
        #include <tmmintrin.h>
    #else
        #include <emmintrin.h>
    #endif
#elif defined(USE_LSX)
    #include <lsxintrin.h>
    #if defined(USE_LASX)
        #include <lasxintrin.h>
    #endif
#elif defined(USE_NEON)
    #include <arm_neon.h>
#elif defined(USE_RVV)
    #include <riscv_vector.h>
#else
    //#pragma message("No SIMD instruction set enabled — falling back to scalar code")
#endif

#include <type_traits>

#include "../misc.h"
#include "../types.h"  // IWYU pragma: keep
#include "ntypes.h"

namespace DON::NNUE::SIMD {

#if defined(USE_AVX2) && !defined(USE_VNNI) && !defined(USE_AVX512)
    #define USE_AVX2_PAIR_ACTIVATIONS
#endif

inline constexpr usize WIDTH_MAX = 32;
inline constexpr usize WIDTH_MIN = 16;

// SIMD width (in bytes)
inline constexpr usize WIDTH =
#if defined(USE_AVX2) || defined(USE_LASX)
  WIDTH_MAX
#elif defined(USE_SSE2) || defined(USE_LSX) || defined(USE_NEON)
  WIDTH_MIN
#else
  0
#endif
  ;

// If vector instructions are enabled, update and refresh the accumulator tile by tile
// such that each tile fits in the CPU's vector registers.
#define VECTOR
// clang-format off
#if defined(USE_SSE2)
    #if defined(USE_AVX512)
using vec_t      = __m512i;
using vec_i8_t   = __m256i;
using psqt_vec_t = __m256i;

        #define vec_load(src) _mm512_load_si512(src)
        #define vec_store(dst, value) _mm512_store_si512(dst, value)
        #define vec_convert_8_16(a) _mm512_cvtepi8_epi16(a)
        #define vec_add_16(a, b) _mm512_add_epi16(a, b)
        #define vec_sub_16(a, b) _mm512_sub_epi16(a, b)
        #define vec_mulhi_16(a, b) _mm512_mulhi_epi16(a, b)
        #define vec_zero() _mm512_setzero_epi32()
        #define vec_set_16(a) _mm512_set1_epi16(a)
        #define vec_max_16(a, b) _mm512_max_epi16(a, b)
        #define vec_min_16(a, b) _mm512_min_epi16(a, b)
        #define vec_slli_16(a, b) _mm512_slli_epi16(a, b)
        // Inverse permuted at load time
        #define vec_packus_16(a, b) _mm512_packus_epi16(a, b)
        #define vec_load_psqt(src) _mm256_load_si256(src)
        #define vec_store_psqt(dst, value) _mm256_store_si256(dst, value)
        #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
        #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
        #define vec_zero_psqt() _mm256_setzero_si256()

        #define vec_nnz(a) _mm512_cmpgt_epi32_mask(a, _mm512_setzero_si512())

        #define MaxRegisterCount 16
        #define MaxChunkSize 64

    #elif defined(USE_AVX2)
using vec_t      = __m256i;
using vec_i8_t   = __m128i;
using psqt_vec_t = __m256i;

        #define vec_load(src) _mm256_load_si256(src)
        #define vec_store(dst, value) _mm256_store_si256(dst, value)
        #define vec_convert_8_16(a) _mm256_cvtepi8_epi16(a)
        #define vec_add_16(a, b) _mm256_add_epi16(a, b)
        #define vec_sub_16(a, b) _mm256_sub_epi16(a, b)
        #define vec_mulhi_16(a, b) _mm256_mulhi_epi16(a, b)
        #define vec_zero() _mm256_setzero_si256()
        #define vec_set_16(a) _mm256_set1_epi16(a)
        #define vec_max_16(a, b) _mm256_max_epi16(a, b)
        #define vec_min_16(a, b) _mm256_min_epi16(a, b)
        #define vec_slli_16(a, b) _mm256_slli_epi16(a, b)
        // Inverse permuted at load time
        #define vec_packus_16(a, b) _mm256_packus_epi16(a, b)
        #define vec_load_psqt(src) _mm256_load_si256(src)
        #define vec_store_psqt(dst, value) _mm256_store_si256(dst, value)
        #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
        #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
        #define vec_zero_psqt() _mm256_setzero_si256()

        #define vec_nnz(a) _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(a, _mm256_setzero_si256())))

        #define MaxRegisterCount 12
        #define MaxChunkSize 32

    #else
using vec_t      = __m128i;
using vec_i8_t   = u64;
using psqt_vec_t = __m128i;

        #define vec_load(src) (*(src))
        #define vec_store(dst, value) *(dst) = (value)

        #if defined(X86_32)  // 32-bit x86?
inline __m128i i386_cvtsi64_si128(const i64 value) noexcept {
    return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&value));
}
            #define _mm_cvtsi64_si128(a) SIMD::i386_cvtsi64_si128(a)
        #endif
        #if defined(USE_SSE41)
            #if defined(__wasm__)
                #define vec_convert_8_16(a) wasm_i16x8_load8x8(reinterpret_cast<const void*>(&a))
            #else
                #define vec_convert_8_16(a) _mm_cvtepi8_epi16(_mm_cvtsi64_si128(static_cast<i64>(a)))
            #endif
        #else
inline __m128i ssse3_cvtepi8_epi16(const u64 a) noexcept {
    const __m128i v8   = _mm_cvtsi64_si128(static_cast<i64>(a));
    const __m128i sign = _mm_cmpgt_epi8(_mm_setzero_si128(), v8);
    return _mm_unpacklo_epi8(v8, sign);
}
            #define vec_convert_8_16(a) SIMD::ssse3_cvtepi8_epi16(a)
        #endif

        #define vec_add_16(a, b) _mm_add_epi16(a, b)
        #define vec_sub_16(a, b) _mm_sub_epi16(a, b)
        #define vec_mulhi_16(a, b) _mm_mulhi_epi16(a, b)
        #define vec_zero() _mm_setzero_si128()
        #define vec_set_16(a) _mm_set1_epi16(a)
        #define vec_max_16(a, b) _mm_max_epi16(a, b)
        #define vec_min_16(a, b) _mm_min_epi16(a, b)
        #define vec_slli_16(a, b) _mm_slli_epi16(a, b)
        #define vec_packus_16(a, b) _mm_packus_epi16(a, b)
        #define vec_load_psqt(src) (*(src))
        #define vec_store_psqt(dst, value) *(dst) = (value)
        #define vec_add_psqt_32(a, b) _mm_add_epi32(a, b)
        #define vec_sub_psqt_32(a, b) _mm_sub_epi32(a, b)
        #define vec_zero_psqt() _mm_setzero_si128()

        #if defined(USE_SSSE3)
            #define vec_nnz(a) _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpgt_epi32(a, _mm_setzero_si128())))
        #endif

        #if defined(IS_64BIT)
            #define MaxRegisterCount 12
        #else
            #define MaxRegisterCount 6
        #endif
        #define MaxChunkSize 16

    #endif

#elif defined(USE_LSX)
    #if defined(USE_LASX)
using vec_t      = __m256i;
using vec_i8_t   = __m128i;
using psqt_vec_t = __m256i;

inline __m256i lasx_load256(const __m256i* src) noexcept {
    return __lasx_xvld(reinterpret_cast<const void*>(src), 0);
}
        #define vec_load(src) SIMD::lasx_load256(src)

inline void lasx_store256(__m256i* dst, const __m256i value) noexcept {
    __lasx_xvst(value, reinterpret_cast<void*>(dst), 0);
}
        #define vec_store(dst, value) SIMD::lasx_store256(dst, value)

inline __m256i lasx_cvtepi8_epi16(const __m128i a) noexcept {
        #if defined(__has_builtin) && __has_builtin(__builtin_lasx_cast_128)
    return __lasx_vext2xv_h_b(__lasx_cast_128(a));
        #elif defined(__GNUC__) && !defined(__clang__)
    __m256i epi16;
    __asm__("vext2xv.h.b %u0, %u1" : "=f"(epi16) : "f"(a));
    return epi16;
        #else
    const auto lo = __lsx_vpickve2gr_d(a, 0);
    const auto hi = __lsx_vpickve2gr_d(a, 1);
    __m256i    v  = __lasx_xvldi(0);
    v             = __lasx_xvinsgr2vr_d(v, lo, 0);
    v             = __lasx_xvinsgr2vr_d(v, hi, 2);
    return __lasx_xvsllwil_h_b(v, 0);
        #endif
}
        #define vec_convert_8_16(a) SIMD::lasx_cvtepi8_epi16(a)

        #define vec_add_16(a, b) __lasx_xvadd_h(a, b)
        #define vec_sub_16(a, b) __lasx_xvsub_h(a, b)
        #define vec_mulhi_16(a, b) __lasx_xvmuh_h(a, b)
        #define vec_zero() __lasx_xvldi(0)
        #define vec_set_16(a) __lasx_xvreplgr2vr_h(a)
        #define vec_max_16(a, b) __lasx_xvmax_h(a, b)
        #define vec_min_16(a, b) __lasx_xvmin_h(a, b)
        #define vec_slli_16(a, b) __lasx_xvslli_h(a, b)
        // Inverse permuted at load time
inline __m256i lasx_packus_16(const __m256i a, const __m256i b) noexcept {
        #if defined(__clang__) && defined(__has_builtin) && __has_builtin(__builtin_lasx_xvssrani_bu_h)
    return (__m256i) __builtin_lasx_xvssrani_bu_h((v32i8) b, (v32i8) a, 0);
        #else
    return __lasx_xvssrani_bu_h(b, a, 0);
        #endif
}
        #define vec_packus_16(a, b) SIMD::lasx_packus_16(a, b)

inline __m256i lasx_packus_32(const __m256i a, const __m256i b) noexcept {
        #if defined(__clang__) && defined(__has_builtin) && __has_builtin(__builtin_lasx_xvssrani_hu_w)
    return (__m256i) __builtin_lasx_xvssrani_hu_w((v16i16) b, (v16i16) a, 0);
        #else
    return __lasx_xvssrani_hu_w(b, a, 0);
        #endif
}
        #define vec_packus_32(a, b) SIMD::lasx_packus_32(a, b)

        #define vec_load_psqt(src) SIMD::lasx_load256(src)
        #define vec_store_psqt(dst, value) SIMD::lasx_store256(dst, value)
        #define vec_add_psqt_32(a, b) __lasx_xvadd_w(a, b)
        #define vec_sub_psqt_32(a, b) __lasx_xvsub_w(a, b)
        #define vec_zero_psqt() __lasx_xvldi(0)

inline int lasx_vec_nnz(const __m256i a) noexcept {
    const __m256i cmp  = __lasx_xvslt_w(__lasx_xvldi(0), a);
    const __m256i mask = __lasx_xvmskltz_w(cmp);
    return (__lasx_xvpickve2gr_w(mask, 0) << 0)
         | (__lasx_xvpickve2gr_w(mask, 4) << 4);
}
        #define vec_nnz(a) SIMD::lasx_vec_nnz(a)

        #define vec_mulhi_8 __lasx_xvmuh_bu
        #define vec_srli_8 __lasx_xvsrli_b

        #define MaxRegisterCount 24
        #define MaxChunkSize 32

    #else
using vec_t      = __m128i;
using vec_i8_t   = u64;
using psqt_vec_t = __m128i;

        #define vec_load(src) (*(src))
        #define vec_store(dst, value) *(dst) = (value)

inline __m128i lsx_cvtepi8_epi16(const u64 x) noexcept {
    __m128i v = __lsx_vldrepl_d(reinterpret_cast<const void*>(&x), 0);
    return __lsx_vsllwil_h_b(v, 0);
}
        #define vec_convert_8_16(a) SIMD::lsx_cvtepi8_epi16(a)

        #define vec_add_16(a, b) __lsx_vadd_h(a, b)
        #define vec_sub_16(a, b) __lsx_vsub_h(a, b)
        #define vec_mulhi_16(a, b) __lsx_vmuh_h(a, b)
        #define vec_zero() __lsx_vldi(0)
        #define vec_set_16(a) __lsx_vreplgr2vr_h(a)
        #define vec_max_16(a, b) __lsx_vmax_h(a, b)
        #define vec_min_16(a, b) __lsx_vmin_h(a, b)
        #define vec_slli_16(a, b) __lsx_vslli_h(a, b)
        // Inverse permuted at load time
inline __m128i lsx_packus_16(const __m128i a, const __m128i b) noexcept {
        #if defined(__clang__) && defined(__has_builtin) && __has_builtin(__builtin_lsx_vssrani_bu_h)
    return (__m128i) __builtin_lsx_vssrani_bu_h((v16i8) b, (v16i8) a, 0);
        #else
    return __lsx_vssrani_bu_h(b, a, 0);
        #endif
}
        #define vec_packus_16(a, b) SIMD::lsx_packus_16(a, b)

inline __m128i lsx_packus_32(const __m128i a, const __m128i b) noexcept {
        #if defined(__clang__) && defined(__has_builtin) && __has_builtin(__builtin_lsx_vssrani_hu_w)
    return (__m128i) __builtin_lsx_vssrani_hu_w((v8i16) b, (v8i16) a, 0);
        #else
    return __lsx_vssrani_hu_w(b, a, 0);
        #endif
}
        #define vec_packus_32(a, b) SIMD::lsx_packus_32(a, b)

        #define vec_load_psqt(src) (*(src))
        #define vec_store_psqt(dst, value) *(dst) = (value)
        #define vec_add_psqt_32(a, b) __lsx_vadd_w(a, b)
        #define vec_sub_psqt_32(a, b) __lsx_vsub_w(a, b)
        #define vec_zero_psqt() __lsx_vldi(0)

inline int lsx_vec_nnz(const __m128i a) noexcept {
    const __m128i cmp  = __lsx_vslt_w(__lsx_vldi(0), a);
    const __m128i mask = __lsx_vmskltz_w(cmp);
    return __lsx_vpickve2gr_w(mask, 0);
}
        #define vec_nnz(a) SIMD::lsx_vec_nnz(a)


        #define vec_mulhi_8 __lsx_vmuh_bu
        #define vec_srli_8 __lsx_vsrli_b

        #define MaxRegisterCount 24
        #define MaxChunkSize 16

    #endif

#elif defined(USE_NEON)
using vec_i8x8_t __attribute__((may_alias))  = int8x8_t;
using vec_i16x8_t __attribute__((may_alias)) = int16x8_t;
using vec_i8x16_t __attribute__((may_alias)) = int8x16_t;
using vec_u16x8_t __attribute__((may_alias)) = uint16x8_t;
using vec_i32x4_t __attribute__((may_alias)) = int32x4_t;
using vec_t __attribute__((may_alias))       = int16x8_t;
using vec_i8_t __attribute__((may_alias))    = int8x16_t;
using psqt_vec_t __attribute__((may_alias))  = int32x4_t;

    #define vec_load(src) (*(src))
    #define vec_store(dst, value) *(dst) = (value)
    #define vec_add_16(a, b) vaddq_s16(a, b)
    #define vec_sub_16(a, b) vsubq_s16(a, b)
    #define vec_mulhi_16(a, b) vqdmulhq_s16(a, b)
    #define vec_zero() vec_t{0}
    #define vec_set_16(a) vdupq_n_s16(a)
    #define vec_max_16(a, b) vmaxq_s16(a, b)
    #define vec_min_16(a, b) vminq_s16(a, b)
    #define vec_slli_16(a, b) vshlq_s16(a, vec_set_16(b))
    #define vec_packus_16(a, b) reinterpret_cast<vec_t>(vcombine_u8(vqmovun_s16(a), vqmovun_s16(b)))
    #define vec_load_psqt(src) (*(src))
    #define vec_store_psqt(dst, value) *(dst) = (value)
    #define vec_add_psqt_32(a, b) vaddq_s32(a, b)
    #define vec_sub_psqt_32(a, b) vsubq_s32(a, b)
    #define vec_zero_psqt() psqt_vec_t{0}

    #define MaxRegisterCount 16
    #define MaxChunkSize 16

    #if defined(__arm__) && !defined(__aarch64__)
// Compatibility wrappers for missing NEON _high widening intrinsics on 32-bit ARM
inline int16x8_t arm32_vaddw_high_s8(const int16x8_t a, const int8x16_t b) noexcept {
    return vaddw_s8(a, vget_high_s8(b));
}
inline int16x8_t arm32_vsubw_high_s8(const int16x8_t a, const int8x16_t b) noexcept {
    return vsubw_s8(a, vget_high_s8(b));
}
        #define vaddw_high_s8(a, b) SIMD::arm32_vaddw_high_s8(a, b)
        #define vsubw_high_s8(a, b) SIMD::arm32_vsubw_high_s8(a, b)
    #endif

#else
    #undef VECTOR

#endif
// clang-format on

struct Vec16Wrapper final {
#if defined(VECTOR)
    using type = vec_t;
    static type add(const type& lhs, const type& rhs) noexcept { return vec_add_16(lhs, rhs); }
    static type sub(const type& lhs, const type& rhs) noexcept { return vec_sub_16(lhs, rhs); }
#else
    using type = BiasType;
    static type add(const type& lhs, const type& rhs) noexcept { return lhs + rhs; }
    static type sub(const type& lhs, const type& rhs) noexcept { return lhs - rhs; }
#endif
};

struct Vec32Wrapper final {
#if defined(VECTOR)
    using type = psqt_vec_t;
    static type add(const type& lhs, const type& rhs) noexcept { return vec_add_psqt_32(lhs, rhs); }
    static type sub(const type& lhs, const type& rhs) noexcept { return vec_sub_psqt_32(lhs, rhs); }
#else
    using type = PSQTWeightType;
    static type add(const type& lhs, const type& rhs) { return lhs + rhs; }
    static type sub(const type& lhs, const type& rhs) { return lhs - rhs; }
#endif
};

enum class UpdateOperation : u8 {
    Add,
    Sub
};

template<typename VecWrapper,
         UpdateOperation... ops,
         std::enable_if_t<sizeof...(ops) == 0, bool> = true>
typename VecWrapper::type fused(const typename VecWrapper::type& in) noexcept {
    return in;
}

template<typename VecWrapper,
         UpdateOperation UpdateOp,
         UpdateOperation... ops,
         typename T,
         typename... Ts,
         std::enable_if_t<is_all_same_v<typename VecWrapper::type, T, Ts...>, bool> = true,
         std::enable_if_t<sizeof...(ops) == sizeof...(Ts), bool>                    = true>
typename VecWrapper::type
fused(const typename VecWrapper::type& in, const T& operand, const Ts&... operands) noexcept {
    static_assert(UpdateOp == UpdateOperation::Add || UpdateOp == UpdateOperation::Sub,
                  "Unsupported UpdateOp.");
    if constexpr (UpdateOp == UpdateOperation::Add)
        return fused<VecWrapper, ops...>(VecWrapper::add(in, operand), operands...);
    if constexpr (UpdateOp == UpdateOperation::Sub)
        return fused<VecWrapper, ops...>(VecWrapper::sub(in, operand), operands...);
    return typename VecWrapper::type();
}

#if defined(USE_SSSE3)
    #if defined(USE_AVX512)
inline int m512_hadd(const __m512i sum, const int bias) noexcept {
    return _mm512_reduce_add_epi32(sum) + bias;
}

inline void m512_add_dpbusd_epi32(__m512i& acc, const __m512i a, const __m512i b) noexcept {
        #if defined(USE_VNNI)
    acc = _mm512_dpbusd_epi32(acc, a, b);
        #else
    __m512i product = _mm512_maddubs_epi16(a, b);
    product         = _mm512_madd_epi16(product, _mm512_set1_epi16(1));
    acc             = _mm512_add_epi32(acc, product);
        #endif
}
    #endif
    #if defined(USE_AVX2)
inline int m256_hadd(const __m256i sum, const int bias) noexcept {
    const __m128i loSum = _mm256_castsi256_si128(sum);
    const __m128i hiSum = _mm256_extracti128_si256(sum, 1);
    __m128i       sm    = _mm_add_epi32(loSum, hiSum);
    sm                  = _mm_add_epi32(sm, _mm_shuffle_epi32(sm, _MM_PERM_BADC));
    sm                  = _mm_add_epi32(sm, _mm_shuffle_epi32(sm, _MM_PERM_CDAB));
    return _mm_cvtsi128_si32(sm) + bias;
}

inline void m256_add_dpbusd_epi32(__m256i& acc, const __m256i a, const __m256i b) noexcept {
        #if defined(USE_VNNI)
    acc = _mm256_dpbusd_epi32(acc, a, b);
        #else
    __m256i product = _mm256_maddubs_epi16(a, b);
    product         = _mm256_madd_epi16(product, _mm256_set1_epi16(1));
    acc             = _mm256_add_epi32(acc, product);
        #endif
}
    #endif
    #if defined(USE_SSSE3)
inline int m128_hadd(const __m128i sum, const int bias) noexcept {
    __m128i sm = sum;
    sm         = _mm_add_epi32(sm, _mm_shuffle_epi32(sm, 0x4E));  //_MM_PERM_BADC
    sm         = _mm_add_epi32(sm, _mm_shuffle_epi32(sm, 0xB1));  //_MM_PERM_CDAB
    return _mm_cvtsi128_si32(sm) + bias;
}

inline void m128_add_dpbusd_epi32(__m128i& acc, const __m128i a, const __m128i b) noexcept {
        #if defined(__wasm_relaxed_simd__)
    acc = wasm_i32x4_relaxed_dot_i8x16_i7x16_add(b, a, acc);
        #else
    __m128i product = _mm_maddubs_epi16(a, b);
    product         = _mm_madd_epi16(product, _mm_set1_epi16(1));
    acc             = _mm_add_epi32(acc, product);
        #endif
}
    #endif
#endif  // USE_SSSE3

#if defined(USE_LSX)
    #if defined(USE_LASX)
inline int lasx_m256_hadd(const __m256i sum, const int bias) noexcept {
    __m256i sm = sum;
    sm         = __lasx_xvadd_w(sm, __lasx_xvshuf4i_w(sm, 0x4E));  // [C,D,A,B] per lane
    sm         = __lasx_xvadd_w(sm, __lasx_xvshuf4i_w(sm, 0xB1));  // [B,A,D,C] per lane
    auto loSm  = __lasx_xvpickve2gr_w(sm, 0);
    auto hiSm  = __lasx_xvpickve2gr_w(sm, 4);
    return loSm + hiSm + bias;
}

inline void lasx_m256_add_dpbusd_epi32(__m256i& acc, const __m256i a, const __m256i b) noexcept {
    __m256i product = __lasx_xvmulwev_h_bu_b(a, b);
    product         = __lasx_xvmaddwod_h_bu_b(product, a, b);
    acc             = __lasx_xvadd_w(acc, __lasx_xvhaddw_w_h(product, product));
}
    #endif
    #if defined(USE_LSX)
inline int lsx_m128_hadd(const __m128i sum, const int bias) noexcept {
    __m128i sm = sum;
    sm         = __lsx_vadd_w(sm, __lsx_vshuf4i_w(sm, 0x4E));  // [C,D,A,B]
    sm         = __lsx_vadd_w(sm, __lsx_vshuf4i_w(sm, 0xB1));  // [B,A,D,C]
    return __lsx_vpickve2gr_w(sm, 0) + bias;
}

inline void lsx_m128_add_dpbusd_epi32(__m128i& acc, const __m128i a, const __m128i b) noexcept {
    // product[i] = a[2i]*b[2i] + a[2i+1]*b[2i+1]
    __m128i product = __lsx_vmulwev_h_bu_b(a, b);
    product         = __lsx_vmaddwod_h_bu_b(product, a, b);
    acc             = __lsx_vadd_w(acc, __lsx_vhaddw_w_h(product, product));
}
    #endif
#endif  // USE_LSX

#if defined(USE_NEON)
inline int neon_m128_reduce_add_epi32(const int32x4_t s) noexcept {
    #if defined(USE_NEON) && USE_NEON >= 8
    return vaddvq_s32(s);
    #else
    return s[0] + s[1] + s[2] + s[3];
    #endif
}

inline int neon_m128_hadd(const int32x4_t sum, const int bias) noexcept {
    return neon_m128_reduce_add_epi32(sum) + bias;
}

    #if defined(USE_NEON_DOTPROD)
inline void
dotprod_m128_add_dpbusd_epi32(int32x4_t& acc, const int8x16_t a, const int8x16_t b) noexcept {
    acc = vdotq_s32(acc, a, b);
}
    #endif
    #if defined(USE_NEON) && USE_NEON >= 8
inline void
neon8_m128_add_dpbusd_epi32(int32x4_t& acc, const int8x16_t a, const int8x16_t b) noexcept {
    const int16x8_t product0 = vmull_s8(vget_low_s8(a), vget_low_s8(b));
    const int16x8_t product1 = vmull_high_s8(a, b);
    const int16x8_t sum      = vpaddq_s16(product0, product1);
    acc                      = vpadalq_s16(acc, sum);
}
    #endif
//     #if defined(USE_NEON) && USE_NEON < 8
// inline void
// neon_m128_add_dpbusd_epi32(int32x4_t& acc, const int8x16_t a, const int8x16_t b) noexcept {
//     const int16x8_t product0 = vmull_s8(vget_low_s8(a), vget_low_s8(b));
//     const int16x8_t product1 = vmull_s8(vget_high_s8(a), vget_high_s8(b));
//     const int16x4_t sum0     = vpadd_s16(vget_low_s16(product0), vget_high_s16(product0));
//     const int16x4_t sum1     = vpadd_s16(vget_low_s16(product1), vget_high_s16(product1));
//     const int16x8_t sum      = vcombine_s16(sum0, sum1);
//     acc                      = vpadalq_s16(acc, sum);
// }
//     #endif
#endif  // USE_NEON

#if defined(USE_RVV)

    #define RVV_DPBUSD(A, W, H) \
        inline vint32##A##_t rvv_dpbusd_##A(vuint8##A##_t a, vint8##A##_t b, usize n) noexcept { \
            vint16##W##_t  prod   = __riscv_vwmulsu_vv_i16##W(b, a, 4 * n); \
            vuint32##W##_t prod32 = __riscv_vreinterpret_v_u16##W##_u32##W( \
              __riscv_vreinterpret_v_i16##W##_u16##W(prod)); \
            vint16##A##_t even = \
              __riscv_vreinterpret_v_u16##A##_i16##A(__riscv_vnsrl_wx_u16##A(prod32, 0, 2 * n)); \
            vint16##A##_t odd = \
              __riscv_vreinterpret_v_u16##A##_i16##A(__riscv_vnsrl_wx_u16##A(prod32, 16, 2 * n)); \
            vuint32##A##_t pairs = __riscv_vreinterpret_v_u16##A##_u32##A( \
              __riscv_vreinterpret_v_i16##A##_u16##A(__riscv_vadd_vv_i16##A(even, odd, 2 * n))); \
            vint16##H##_t lo = \
              __riscv_vreinterpret_v_u16##H##_i16##H(__riscv_vnsrl_wx_u16##H(pairs, 0, n)); \
            vint16##H##_t hi = \
              __riscv_vreinterpret_v_u16##H##_i16##H(__riscv_vnsrl_wx_u16##H(pairs, 16, n)); \
            return __riscv_vwadd_vv_i32##A(lo, hi, n); \
        }

RVV_DPBUSD(m1, m2, mf2)
RVV_DPBUSD(m2, m4, m1)
RVV_DPBUSD(m4, m8, m2)

    #undef RVV_DPBUSD

#endif  // USE_RVV

// Compute optimal SIMD register count for feature transformer accumulation
template<IndexType TransformedFeatureWidth, IndexType HalfDimensions, IndexType PSQTBuckets>
class Tiling final {
   private:
#if defined(VECTOR)
    // Use __m* types as template arguments, which causes GCC to emit warnings about losing some attribute information.
    // This is irrelevant to us as only take their size, so the following pragma are harmless.
    #if defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wignored-attributes"
    #endif

    template<typename RegisterType, typename LaneType, usize LaneCount, usize RegisterCount>
    static constexpr usize best_register_count() noexcept {
        constexpr usize RegisterSize = sizeof(RegisterType);
        constexpr usize LaneSize     = sizeof(LaneType);

        static_assert(RegisterSize >= LaneSize);
        static_assert(RegisterCount <= MaxRegisterCount);
        static_assert(RegisterCount > 0);
        static_assert(MaxRegisterCount > 0);
        static_assert(RegisterSize % LaneSize == 0);
        static_assert((LaneCount * LaneSize) % RegisterSize == 0);

        usize ideal = (LaneCount * LaneSize) / RegisterSize;
        if (ideal <= RegisterCount)
            return ideal;

        // Look for the largest divisor of the ideal register count that is smaller than RegisterCount
        for (usize divisor = RegisterCount; divisor > 1; --divisor)
            if (ideal % divisor == 0)
                return divisor;

        return 1;
    }

    #if defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif

   public:
    static constexpr usize RegCount =
      best_register_count<vec_t, WeightType, TransformedFeatureWidth, MaxRegisterCount>();
    static constexpr usize PSQTRegCount =
      best_register_count<psqt_vec_t, PSQTWeightType, PSQTBuckets, MaxRegisterCount>();

    static constexpr IndexType TileHeight     = RegCount * sizeof(vec_t) / 2;
    static constexpr IndexType PSQTTileHeight = PSQTRegCount * sizeof(psqt_vec_t) / 4;

    static_assert(HalfDimensions % TileHeight == 0, "TileHeight must divide HalfDimensions");
    static_assert(PSQTBuckets % PSQTTileHeight == 0, "PSQTTileHeight must divide PSQTBuckets");
#endif

   private:
    Tiling() noexcept                         = delete;
    ~Tiling() noexcept                        = delete;
    Tiling(const Tiling&) noexcept            = delete;
    Tiling& operator=(const Tiling&) noexcept = delete;
    Tiling(Tiling&&) noexcept                 = delete;
    Tiling& operator=(Tiling&&) noexcept      = delete;
};

}  // namespace DON::NNUE::SIMD

#endif  // NNUE_SIMD_H_INCLUDED

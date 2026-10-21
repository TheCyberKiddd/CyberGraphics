#pragma once

#include <cyber/math/types.h>

#ifndef CG_MATH_SIMD_SSE2
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define CG_MATH_SIMD_SSE2 1
#else
#define CG_MATH_SIMD_SSE2 0
#endif
#endif

#if CG_MATH_SIMD_SSE2
#include <emmintrin.h>
#endif

namespace cyber::math
{

enum class SimdBackend : u8
{
    Scalar,
    Sse2,
};

// The backend is selected at compile time. It is a diagnostic query, not a
// runtime switch: the public API produces the same results on both paths.
[[nodiscard]] constexpr SimdBackend ActiveSimdBackend() noexcept
{
#if CG_MATH_SIMD_SSE2
    return SimdBackend::Sse2;
#else
    return SimdBackend::Scalar;
#endif
}

}  // namespace cyber::math

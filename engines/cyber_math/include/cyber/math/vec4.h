#pragma once

#include <cyber/core/types.h>
#include <cyber/math/constants.h>
#include <cyber/math/simd.h>

#include <cmath>
#include <cstring>

namespace cyber::math
{

struct alignas(16) Vec4
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    constexpr Vec4() noexcept = default;

    constexpr Vec4(f32 xValue, f32 yValue, f32 zValue, f32 wValue) noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }

    explicit constexpr Vec4(f32 scalar) noexcept : x(scalar), y(scalar), z(scalar), w(scalar) {}

    [[nodiscard]] constexpr f32& operator[](usize index) noexcept
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        case 2:
            return z;
        default:
            return w;
        }
    }

    [[nodiscard]] constexpr const f32& operator[](usize index) const noexcept
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        case 2:
            return z;
        default:
            return w;
        }
    }

    [[nodiscard]] static constexpr Vec4 Zero() noexcept
    {
        return Vec4{};
    }

    [[nodiscard]] static constexpr Vec4 One() noexcept
    {
        return Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    }

    // Unaligned load for external vertex and file data. The destination type is
    // still aligned; only the source pointer may be unaligned.
    [[nodiscard]] static Vec4 FromArray(const f32* values) noexcept
    {
        Vec4 result;
        std::memcpy(&result.x, values, sizeof(f32) * 4);
        return result;
    }

    void ToArray(f32* values) const noexcept
    {
        std::memcpy(values, &x, sizeof(f32) * 4);
    }

    [[nodiscard]] Vec4 operator+(const Vec4& rhs) const noexcept
    {
#if CG_MATH_SIMD_SSE2
        return FromSimd(_mm_add_ps(Simd(), rhs.Simd()));
#else
        return Vec4{x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
#endif
    }

    [[nodiscard]] Vec4 operator-(const Vec4& rhs) const noexcept
    {
#if CG_MATH_SIMD_SSE2
        return FromSimd(_mm_sub_ps(Simd(), rhs.Simd()));
#else
        return Vec4{x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
#endif
    }

    [[nodiscard]] Vec4 operator-() const noexcept
    {
#if CG_MATH_SIMD_SSE2
        return FromSimd(_mm_sub_ps(_mm_setzero_ps(), Simd()));
#else
        return Vec4{-x, -y, -z, -w};
#endif
    }

    [[nodiscard]] Vec4 operator*(f32 scalar) const noexcept
    {
#if CG_MATH_SIMD_SSE2
        return FromSimd(_mm_mul_ps(Simd(), _mm_set1_ps(scalar)));
#else
        return Vec4{x * scalar, y * scalar, z * scalar, w * scalar};
#endif
    }

    [[nodiscard]] Vec4 operator/(f32 scalar) const noexcept
    {
#if CG_MATH_SIMD_SSE2
        return FromSimd(_mm_div_ps(Simd(), _mm_set1_ps(scalar)));
#else
        return Vec4{x / scalar, y / scalar, z / scalar, w / scalar};
#endif
    }

    Vec4& operator+=(const Vec4& rhs) noexcept
    {
        *this = *this + rhs;
        return *this;
    }

    Vec4& operator-=(const Vec4& rhs) noexcept
    {
        *this = *this - rhs;
        return *this;
    }

    Vec4& operator*=(f32 scalar) noexcept
    {
        *this = *this * scalar;
        return *this;
    }

    Vec4& operator/=(f32 scalar) noexcept
    {
        *this = *this / scalar;
        return *this;
    }

#if CG_MATH_SIMD_SSE2
    // Implementation detail, public only so the inline SIMD operations and the
    // math tests can share one load/store path.
    [[nodiscard]] __m128 Simd() const noexcept
    {
        return _mm_load_ps(&x);
    }

    [[nodiscard]] static Vec4 FromSimd(__m128 value) noexcept
    {
        Vec4 result;
        _mm_store_ps(&result.x, value);
        return result;
    }
#endif
};

[[nodiscard]] inline Vec4 operator*(f32 scalar, const Vec4& value) noexcept
{
    return value * scalar;
}

[[nodiscard]] inline f32 Dot(const Vec4& lhs, const Vec4& rhs) noexcept
{
#if CG_MATH_SIMD_SSE2
    const __m128 product = _mm_mul_ps(lhs.Simd(), rhs.Simd());
    const __m128 pairs =
        _mm_add_ps(product, _mm_shuffle_ps(product, product, _MM_SHUFFLE(2, 3, 0, 1)));
    const __m128 total = _mm_add_ss(pairs, _mm_shuffle_ps(pairs, pairs, _MM_SHUFFLE(1, 0, 3, 2)));
    return _mm_cvtss_f32(total);
#else
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
#endif
}

[[nodiscard]] inline f32 LengthSquared(const Vec4& value) noexcept
{
    return Dot(value, value);
}

[[nodiscard]] inline f32 Length(const Vec4& value) noexcept
{
    return std::sqrt(LengthSquared(value));
}

[[nodiscard]] inline Vec4 Normalize(const Vec4& value) noexcept
{
    const f32 length = Length(value);
    return length > 0.0f ? value / length : Vec4::Zero();
}

[[nodiscard]] inline Vec4 Lerp(const Vec4& from, const Vec4& to, f32 factor) noexcept
{
    return from + (to - from) * factor;
}

[[nodiscard]] inline Vec4 Min(const Vec4& lhs, const Vec4& rhs) noexcept
{
#if CG_MATH_SIMD_SSE2
    return Vec4::FromSimd(_mm_min_ps(lhs.Simd(), rhs.Simd()));
#else
    return Vec4{Min(lhs.x, rhs.x), Min(lhs.y, rhs.y), Min(lhs.z, rhs.z), Min(lhs.w, rhs.w)};
#endif
}

[[nodiscard]] inline Vec4 Max(const Vec4& lhs, const Vec4& rhs) noexcept
{
#if CG_MATH_SIMD_SSE2
    return Vec4::FromSimd(_mm_max_ps(lhs.Simd(), rhs.Simd()));
#else
    return Vec4{Max(lhs.x, rhs.x), Max(lhs.y, rhs.y), Max(lhs.z, rhs.z), Max(lhs.w, rhs.w)};
#endif
}

[[nodiscard]] inline Vec4 Abs(const Vec4& value) noexcept
{
#if CG_MATH_SIMD_SSE2
    const __m128 signMask = _mm_set1_ps(-0.0f);
    return Vec4::FromSimd(_mm_andnot_ps(signMask, value.Simd()));
#else
    return Vec4{Abs(value.x), Abs(value.y), Abs(value.z), Abs(value.w)};
#endif
}

[[nodiscard]] inline bool ApproxEqual(const Vec4& lhs, const Vec4& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.x, rhs.x, tolerance) && ApproxEqual(lhs.y, rhs.y, tolerance) &&
           ApproxEqual(lhs.z, rhs.z, tolerance) && ApproxEqual(lhs.w, rhs.w, tolerance);
}

static_assert(sizeof(Vec4) == sizeof(f32) * 4, "Vec4 must have no padding");
static_assert(alignof(Vec4) == 16, "Vec4 must be 16-byte aligned");

}  // namespace cyber::math

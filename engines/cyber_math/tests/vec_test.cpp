#include <cyber/math/math.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstddef>

using namespace cyber::math;

CG_TEST(math_vec2_basic)
{
    const Vec2 a{3.0f, 4.0f};
    const Vec2 b{1.0f, 2.0f};

    CG_EXPECT_NEAR(a.x, 3.0f, 0.0f);
    CG_EXPECT_NEAR(a[1], 4.0f, 0.0f);
    CG_EXPECT_TRUE(ApproxEqual(a + b, Vec2{4.0f, 6.0f}));
    CG_EXPECT_TRUE(ApproxEqual(a - b, Vec2{2.0f, 2.0f}));
    CG_EXPECT_TRUE(ApproxEqual(-a, Vec2{-3.0f, -4.0f}));
    CG_EXPECT_TRUE(ApproxEqual(a * 2.0f, Vec2{6.0f, 8.0f}));
    CG_EXPECT_TRUE(ApproxEqual(2.0f * a, Vec2{6.0f, 8.0f}));
    CG_EXPECT_TRUE(ApproxEqual(a / 2.0f, Vec2{1.5f, 2.0f}));
    CG_EXPECT_NEAR(Dot(a, b), 11.0f, 1.0e-6f);
    CG_EXPECT_NEAR(LengthSquared(a), 25.0f, 1.0e-6f);
    CG_EXPECT_NEAR(Length(a), 5.0f, 1.0e-6f);
    CG_EXPECT_TRUE(ApproxEqual(Normalize(a), Vec2{0.6f, 0.8f}));
    CG_EXPECT_TRUE(ApproxEqual(Normalize(Vec2::Zero()), Vec2::Zero()));
    CG_EXPECT_TRUE(ApproxEqual(Lerp(a, b, 0.5f), Vec2{2.0f, 3.0f}));
    CG_EXPECT_TRUE(ApproxEqual(Min(a, b), b));
    CG_EXPECT_TRUE(ApproxEqual(Max(a, b), a));
    CG_EXPECT_TRUE(ApproxEqual(Abs(Vec2{-1.0f, 2.0f}), Vec2{1.0f, 2.0f}));

    CG_EXPECT_FALSE(ApproxEqual(1.0f, 1.0f, -1.0f));
    CG_EXPECT_FALSE(ApproxEqual(Vec2::Zero(), Vec2::Zero(), -1.0f));
}

CG_TEST(math_vec3_basic)
{
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    const Vec3 z{0.0f, 0.0f, 1.0f};
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};

    CG_EXPECT_TRUE(ApproxEqual(Cross(x, y), z));
    CG_EXPECT_TRUE(ApproxEqual(Cross(y, z), x));
    CG_EXPECT_TRUE(ApproxEqual(Cross(z, x), y));
    CG_EXPECT_NEAR(Dot(a, b), 32.0f, 1.0e-6f);
    CG_EXPECT_NEAR(Length(a), std::sqrt(14.0f), 1.0e-6f);
    CG_EXPECT_TRUE(ApproxEqual(a + b, Vec3{5.0f, 7.0f, 9.0f}));
    CG_EXPECT_TRUE(ApproxEqual(a - b, Vec3{-3.0f, -3.0f, -3.0f}));
    CG_EXPECT_TRUE(ApproxEqual(a * 2.0f, Vec3{2.0f, 4.0f, 6.0f}));
    CG_EXPECT_TRUE(ApproxEqual(Normalize(Vec3{0.0f, 0.0f, 0.0f}), Vec3::Zero()));

    const Vec3 reflected = Reflect(Vec3{1.0f, -1.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    CG_EXPECT_TRUE(ApproxEqual(reflected, Vec3{1.0f, 1.0f, 0.0f}));

    Vec3 accumulated = Vec3::Zero();
    accumulated += a;
    accumulated -= b;
    accumulated *= 2.0f;
    accumulated /= 2.0f;
    CG_EXPECT_TRUE(ApproxEqual(accumulated, a - b));
}

CG_TEST(math_vec4_arithmetic_and_simd_agreement)
{
    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};

    CG_EXPECT_TRUE(ApproxEqual(a + b, Vec4{6.0f, 8.0f, 10.0f, 12.0f}));
    CG_EXPECT_TRUE(ApproxEqual(a - b, Vec4{-4.0f, -4.0f, -4.0f, -4.0f}));
    CG_EXPECT_TRUE(ApproxEqual(-a, Vec4{-1.0f, -2.0f, -3.0f, -4.0f}));
    CG_EXPECT_TRUE(ApproxEqual(a * 2.0f, Vec4{2.0f, 4.0f, 6.0f, 8.0f}));
    CG_EXPECT_TRUE(ApproxEqual(2.0f * a, Vec4{2.0f, 4.0f, 6.0f, 8.0f}));
    CG_EXPECT_TRUE(ApproxEqual(b / 2.0f, Vec4{2.5f, 3.0f, 3.5f, 4.0f}));

    const Vec4 scalarSum{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    const Vec4 scalarDifference{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    const Vec4 scalarProduct{a.x * 2.0f, a.y * 2.0f, a.z * 2.0f, a.w * 2.0f};
    CG_EXPECT_TRUE(ApproxEqual(a + b, scalarSum));
    CG_EXPECT_TRUE(ApproxEqual(a - b, scalarDifference));
    CG_EXPECT_TRUE(ApproxEqual(a * 2.0f, scalarProduct));

    const f32 expectedDot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    CG_EXPECT_NEAR(Dot(a, b), expectedDot, 1.0e-6f);
    CG_EXPECT_NEAR(LengthSquared(a), 30.0f, 1.0e-6f);
    CG_EXPECT_NEAR(Length(a), std::sqrt(30.0f), 1.0e-6f);

    const f32 inverseLength = 1.0f / std::sqrt(30.0f);
    CG_EXPECT_TRUE(ApproxEqual(Normalize(a), Vec4{a.x * inverseLength, a.y * inverseLength,
                                                  a.z * inverseLength, a.w * inverseLength}));
    CG_EXPECT_TRUE(ApproxEqual(Normalize(Vec4::Zero()), Vec4::Zero()));

    CG_EXPECT_TRUE(ApproxEqual(Min(a, b), a));
    CG_EXPECT_TRUE(ApproxEqual(Max(a, b), b));
    CG_EXPECT_TRUE(ApproxEqual(Abs(Vec4{-1.0f, 2.0f, -3.0f, 4.0f}), a));
    CG_EXPECT_TRUE(ApproxEqual(Lerp(a, b, 0.5f), Vec4{3.0f, 4.0f, 5.0f, 6.0f}));

    Vec4 accumulated = Vec4::Zero();
    accumulated += a;
    accumulated -= b;
    accumulated *= 2.0f;
    accumulated /= 2.0f;
    CG_EXPECT_TRUE(ApproxEqual(accumulated, a - b));
}

CG_TEST(math_vec4_unaligned_array_access)
{
    alignas(4) f32 data[6]{-1.0f, 1.0f, 2.0f, 3.0f, 4.0f, -1.0f};
    const Vec4 loaded = Vec4::FromArray(data + 1);
    CG_EXPECT_TRUE(ApproxEqual(loaded, Vec4{1.0f, 2.0f, 3.0f, 4.0f}));

    f32 output[4]{};
    loaded.ToArray(output);
    for (std::size_t index = 0; index < 4; ++index)
    {
        CG_EXPECT_NEAR(output[index], static_cast<f32>(index + 1), 0.0f);
    }
}

CG_TEST(math_simd_backend_is_compile_time_selected)
{
#if CG_MATH_SIMD_SSE2
    CG_EXPECT_TRUE(ActiveSimdBackend() == SimdBackend::Sse2);
#else
    CG_EXPECT_TRUE(ActiveSimdBackend() == SimdBackend::Scalar);
#endif
    CG_EXPECT_EQ(sizeof(Vec4), std::size_t{16});
    CG_EXPECT_EQ(alignof(Vec4), std::size_t{16});
    CG_EXPECT_EQ(sizeof(Mat4), std::size_t{64});
    CG_EXPECT_EQ(alignof(Mat4), std::size_t{16});
}

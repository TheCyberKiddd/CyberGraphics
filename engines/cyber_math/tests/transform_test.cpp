#include <cyber/math/math.h>
#include <cyber/test/test.h>

using namespace cyber::math;

CG_TEST(math_transform_identity)
{
    const Transform transform = Transform::Identity();
    CG_EXPECT_TRUE(ApproxEqual(transform.translation, Vec3::Zero()));
    CG_EXPECT_TRUE(ApproxEqual(transform.rotation, Quat::Identity()));
    CG_EXPECT_TRUE(ApproxEqual(transform.scale, Vec3::One()));
    CG_EXPECT_TRUE(ApproxEqual(transform.ToMatrix(), Mat4::Identity()));
    CG_EXPECT_TRUE(
        ApproxEqual(transform.TransformPoint(Vec3{1.0f, 2.0f, 3.0f}), Vec3{1.0f, 2.0f, 3.0f}));
}

CG_TEST(math_transform_trs_matches_matrix)
{
    Transform transform;
    transform.translation = Vec3{1.0f, 2.0f, 3.0f};
    transform.rotation = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
    transform.scale = Vec3{2.0f, 2.0f, 2.0f};

    const Vec3 point{1.0f, 0.0f, 0.0f};
    const Vec3 transformed = transform.TransformPoint(point);
    CG_EXPECT_TRUE(ApproxEqual(transformed, Vec3{1.0f, 4.0f, 3.0f}, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(transform.ToMatrix().TransformPoint(point), transformed, 1.0e-5f));

    const Vec3 direction{1.0f, 0.0f, 0.0f};
    CG_EXPECT_TRUE(
        ApproxEqual(transform.TransformVector(direction), Vec3{0.0f, 2.0f, 0.0f}, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(transform.ToMatrix().TransformVector(direction),
                               transform.TransformVector(direction), 1.0e-5f));
}

CG_TEST(math_transform_composition_via_matrix)
{
    Transform first;
    first.translation = Vec3{1.0f, 0.0f, 0.0f};
    first.rotation = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
    first.scale = Vec3{2.0f, 2.0f, 2.0f};

    Transform second;
    second.translation = Vec3{0.0f, 3.0f, 0.0f};
    second.rotation = Quat::FromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, kHalfPi);
    second.scale = Vec3{0.5f, 0.5f, 0.5f};

    const Mat4 composed = first.ToMatrix() * second.ToMatrix();
    const Vec3 point{0.5f, -1.0f, 2.0f};
    CG_EXPECT_TRUE(ApproxEqual(
        composed.TransformPoint(point),
        first.ToMatrix().TransformPoint(second.ToMatrix().TransformPoint(point)), 1.0e-5f));
}

CG_TEST(math_transform_inverse)
{
    Transform transform;
    transform.translation = Vec3{3.0f, -2.0f, 5.0f};
    transform.rotation = Quat::FromAxisAngle(Normalize(Vec3{1.0f, 2.0f, -1.0f}), 0.9f);
    transform.scale = Vec3{2.0f, 0.5f, 4.0f};

    Mat4 inverse;
    CG_REQUIRE(transform.TryInverse(inverse));

    const Vec3 point{0.5f, -1.0f, 2.0f};
    const Vec3 roundTrip = inverse.TransformPoint(transform.ToMatrix().TransformPoint(point));
    CG_EXPECT_TRUE(ApproxEqual(roundTrip, point, 1.0e-4f));
    CG_EXPECT_TRUE(ApproxEqual(inverse * transform.ToMatrix(), Mat4::Identity(), 1.0e-4f));
}

CG_TEST(math_transform_inverse_rejects_zero_scale)
{
    Transform transform;
    transform.scale = Vec3{1.0f, 0.0f, 1.0f};
    transform.translation = Vec3{4.0f, 5.0f, 6.0f};

    const Mat4 untouched = Mat4::Translation(Vec3{9.0f, 9.0f, 9.0f});
    Mat4 inverse = untouched;
    CG_EXPECT_FALSE(transform.TryInverse(inverse));
    CG_EXPECT_TRUE(ApproxEqual(inverse, untouched));
}

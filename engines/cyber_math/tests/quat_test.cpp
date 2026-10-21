#include <cyber/math/math.h>
#include <cyber/test/test.h>

#include <cmath>

using namespace cyber::math;

namespace
{

void ExpectSameRotation(const Quat& lhs, const Quat& rhs, f32 tolerance = 1.0e-5f)
{
    CG_EXPECT_NEAR(std::abs(lhs.Dot(rhs)), 1.0f, tolerance);
}

}  // namespace

CG_TEST(math_quat_axis_angle_and_rotation)
{
    const Quat identity = Quat::Identity();
    CG_EXPECT_TRUE(
        ApproxEqual(identity.Rotate(Vec3{1.0f, 2.0f, 3.0f}), Vec3{1.0f, 2.0f, 3.0f}, 1.0e-5f));

    const Quat quarterTurn = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
    CG_EXPECT_TRUE(
        ApproxEqual(quarterTurn.Rotate(Vec3{1.0f, 0.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(quarterTurn.Conjugate().Rotate(Vec3{0.0f, 1.0f, 0.0f}),
                               Vec3{1.0f, 0.0f, 0.0f}, 1.0e-5f));

    const Quat zeroAxis = Quat::FromAxisAngle(Vec3::Zero(), 1.0f);
    CG_EXPECT_TRUE(ApproxEqual(zeroAxis, Quat::Identity()));
}

CG_TEST(math_quat_composition_order)
{
    const Quat aroundZ = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
    const Quat aroundX = Quat::FromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, kHalfPi);

    const Quat zThenX = aroundZ * aroundX;
    CG_EXPECT_TRUE(
        ApproxEqual(zThenX.Rotate(Vec3{0.0f, 0.0f, 1.0f}), Vec3{1.0f, 0.0f, 0.0f}, 1.0e-5f));

    const Quat xThenZ = aroundX * aroundZ;
    CG_EXPECT_TRUE(
        ApproxEqual(xThenZ.Rotate(Vec3{0.0f, 0.0f, 1.0f}), Vec3{0.0f, -1.0f, 0.0f}, 1.0e-5f));
}

CG_TEST(math_quat_matrix_round_trip)
{
    const Quat original = Quat::FromAxisAngle(Normalize(Vec3{1.0f, -2.0f, 0.5f}), 1.2f).Normalize();
    const Mat3 matrix = original.ToMat3();
    const Quat roundTrip = Quat::FromMat3(matrix);
    ExpectSameRotation(original, roundTrip);

    const Quat fromMat4 = Quat::FromMat4(original.ToMat4());
    ExpectSameRotation(original, fromMat4);

    const Mat4 matrix4 = original.ToMat4();
    CG_EXPECT_TRUE(ApproxEqual(matrix4.TransformVector(Vec3{1.0f, 0.0f, 0.0f}),
                               original.Rotate(Vec3{1.0f, 0.0f, 0.0f}), 1.0e-5f));
}

CG_TEST(math_quat_normalize_and_interpolation)
{
    const Quat dirty{0.0f, 0.0f, 2.0f, 2.0f};
    const Quat normalized = dirty.Normalize();
    CG_EXPECT_NEAR(normalized.Length(), 1.0f, 1.0e-6f);
    CG_EXPECT_TRUE(ApproxEqual(Quat{0.0f, 0.0f, 0.0f, 0.0f}.Normalize(), Quat::Identity()));

    const Quat identity = Quat::Identity();
    const Quat quarterTurn = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);

    const Quat halfway = Quat::Slerp(identity, quarterTurn, 0.5f);
    const Vec3 rotated = halfway.Rotate(Vec3{1.0f, 0.0f, 0.0f});
    const f32 rootHalf = std::sqrt(0.5f);
    CG_EXPECT_TRUE(ApproxEqual(rotated, Vec3{rootHalf, rootHalf, 0.0f}, 1.0e-5f));

    const Quat linear = Quat::Nlerp(identity, quarterTurn, 0.5f);
    CG_EXPECT_TRUE(ApproxEqual(linear.Rotate(Vec3{1.0f, 0.0f, 0.0f}),
                               Vec3{rootHalf, rootHalf, 0.0f}, 1.0e-5f));

    const Quat negative = Quat{-quarterTurn.x, -quarterTurn.y, -quarterTurn.z, -quarterTurn.w};
    CG_EXPECT_TRUE(ApproxEqual(Quat::Slerp(identity, negative, 0.5f).Rotate(Vec3{1.0f, 0.0f, 0.0f}),
                               Vec3{rootHalf, rootHalf, 0.0f}, 1.0e-5f));
}

CG_TEST(math_quat_from_to)
{
    const Quat toY = Quat::FromTo(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    CG_EXPECT_TRUE(
        ApproxEqual(toY.Rotate(Vec3{1.0f, 0.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}, 1.0e-5f));

    const Quat toNegativeX = Quat::FromTo(Vec3{1.0f, 0.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f});
    CG_EXPECT_TRUE(
        ApproxEqual(toNegativeX.Rotate(Vec3{1.0f, 0.0f, 0.0f}), Vec3{-1.0f, 0.0f, 0.0f}, 1.0e-5f));

    CG_EXPECT_TRUE(
        ApproxEqual(Quat::FromTo(Vec3::Zero(), Vec3{1.0f, 0.0f, 0.0f}), Quat::Identity()));
    CG_EXPECT_TRUE(ApproxEqual(Quat::FromTo(Vec3{1.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}),
                               Quat::Identity()));
}

CG_TEST(math_quat_yxz_euler_input)
{
    const Quat aroundY = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, kHalfPi);
    const Quat aroundX = Quat::FromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, kHalfPi);
    const Quat aroundZ = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);

    ExpectSameRotation(Quat::FromEuler(kHalfPi, 0.0f, 0.0f), aroundY);
    ExpectSameRotation(Quat::FromEuler(0.0f, kHalfPi, 0.0f), aroundX);
    ExpectSameRotation(Quat::FromEuler(0.0f, 0.0f, kHalfPi), aroundZ);

    const f32 yaw = 0.3f;
    const f32 pitch = -0.4f;
    const f32 roll = 0.7f;
    const Quat composed = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw) *
                          Quat::FromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitch) *
                          Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, roll);
    ExpectSameRotation(Quat::FromEuler(yaw, pitch, roll), composed);
}

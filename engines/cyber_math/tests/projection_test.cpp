#include <cyber/math/math.h>
#include <cyber/test/test.h>

#include <cmath>

using namespace cyber::math;

CG_TEST(math_projection_perspective_reverse_z)
{
    const f32 nearPlane = 0.1f;
    const f32 farPlane = 100.0f;
    const Mat4 projection = Mat4::PerspectiveReverseZ(kHalfPi, 1.0f, nearPlane, farPlane);

    const Vec3 atNear = projection.TransformPoint(Vec3{0.0f, 0.0f, -nearPlane});
    const Vec3 atFar = projection.TransformPoint(Vec3{0.0f, 0.0f, -farPlane});
    CG_EXPECT_NEAR(atNear.z, 1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(atFar.z, 0.0f, 1.0e-5f);

    const Vec3 top = projection.TransformPoint(Vec3{0.0f, 1.0f, -1.0f});
    CG_EXPECT_NEAR(top.y, 1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(top.x, 0.0f, 1.0e-5f);

    const Vec3 right = projection.TransformPoint(Vec3{1.0f, 0.0f, -1.0f});
    CG_EXPECT_NEAR(right.x, 1.0f, 1.0e-5f);

    const Mat4 wideProjection = Mat4::PerspectiveReverseZ(kHalfPi, 2.0f, nearPlane, farPlane);
    const Vec3 wideRight = wideProjection.TransformPoint(Vec3{2.0f, 0.0f, -1.0f});
    CG_EXPECT_NEAR(wideRight.x, 1.0f, 1.0e-5f);
}

CG_TEST(math_projection_infinite_perspective_reverse_z)
{
    const f32 nearPlane = 0.1f;
    const Mat4 projection = Mat4::PerspectiveInfiniteReverseZ(kHalfPi, 1.0f, nearPlane);

    const Vec3 atNear = projection.TransformPoint(Vec3{0.0f, 0.0f, -nearPlane});
    CG_EXPECT_NEAR(atNear.z, 1.0f, 1.0e-5f);

    const Vec3 veryFar = projection.TransformPoint(Vec3{0.0f, 0.0f, -1.0e6f});
    CG_EXPECT_TRUE(veryFar.z >= 0.0f);
    CG_EXPECT_TRUE(veryFar.z < 1.0e-4f);
}

CG_TEST(math_projection_orthographic_reverse_z)
{
    const Mat4 projection = Mat4::OrthographicReverseZ(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 100.0f);

    const Vec3 nearLeftBottom = projection.TransformPoint(Vec3{-2.0f, -1.0f, -0.1f});
    const Vec3 farRightTop = projection.TransformPoint(Vec3{2.0f, 1.0f, -100.0f});
    const Vec3 center = projection.TransformPoint(Vec3{0.0f, 0.0f, -50.05f});

    CG_EXPECT_NEAR(nearLeftBottom.x, -1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(nearLeftBottom.y, -1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(nearLeftBottom.z, 1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(farRightTop.x, 1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(farRightTop.y, 1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(farRightTop.z, 0.0f, 1.0e-5f);
    CG_EXPECT_NEAR(center.x, 0.0f, 1.0e-5f);
    CG_EXPECT_NEAR(center.y, 0.0f, 1.0e-5f);
    CG_EXPECT_NEAR(center.z, 0.5f, 1.0e-5f);
}

CG_TEST(math_view_look_at_is_right_handed)
{
    const Vec3 eye{0.0f, 0.0f, 5.0f};
    const Vec3 target{0.0f, 0.0f, 0.0f};
    const Mat4 view = Mat4::LookAt(eye, target, Vec3{0.0f, 1.0f, 0.0f});

    const Vec3 eyeInView = view.TransformPoint(eye);
    const Vec3 targetInView = view.TransformPoint(target);
    const Vec3 rightInView = view.TransformVector(Vec3{1.0f, 0.0f, 0.0f});
    const Vec3 upInView = view.TransformVector(Vec3{0.0f, 1.0f, 0.0f});

    CG_EXPECT_TRUE(ApproxEqual(eyeInView, Vec3{0.0f, 0.0f, 0.0f}, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(targetInView, Vec3{0.0f, 0.0f, -5.0f}, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(rightInView, Vec3{1.0f, 0.0f, 0.0f}, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(upInView, Vec3{0.0f, 1.0f, 0.0f}, 1.0e-5f));

    const Mat4 degenerate =
        Mat4::LookAt(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    CG_EXPECT_TRUE(ApproxEqual(degenerate, Mat4::Identity()));

    const Mat4 parallelUp =
        Mat4::LookAt(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    CG_EXPECT_TRUE(ApproxEqual(parallelUp, Mat4::Identity()));
}

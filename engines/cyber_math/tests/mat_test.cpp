#include <cyber/math/math.h>
#include <cyber/test/test.h>

#include <cmath>

using namespace cyber::math;

CG_TEST(math_mat3_basic)
{
    const Mat3 identity = Mat3::Identity();
    const Mat3 scale{Vec3{2.0f, 0.0f, 0.0f}, Vec3{0.0f, 3.0f, 0.0f}, Vec3{0.0f, 0.0f, 4.0f}};

    CG_EXPECT_TRUE(ApproxEqual(identity * identity, identity));
    CG_EXPECT_TRUE(ApproxEqual(scale * Vec3{1.0f, 1.0f, 1.0f}, Vec3{2.0f, 3.0f, 4.0f}));
    CG_EXPECT_NEAR(scale.Determinant(), 24.0f, 1.0e-5f);
    CG_EXPECT_TRUE(ApproxEqual(scale.Transpose(), scale));

    Mat3 inverse;
    CG_REQUIRE(scale.TryInverse(inverse));
    CG_EXPECT_TRUE(ApproxEqual(scale * inverse, identity, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(inverse * scale, identity, 1.0e-5f));

    const Mat3 rotation = UpperLeft3x3(Mat4::RotationZ(kHalfPi));
    CG_EXPECT_TRUE(ApproxEqual(rotation * Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 1.0e-5f));
}

CG_TEST(math_mat4_composition_and_transform)
{
    const Mat4 translation = Mat4::Translation(Vec3{1.0f, 2.0f, 3.0f});
    const Mat4 rotation = Mat4::RotationZ(kHalfPi);
    const Mat4 scale = Mat4::Scale(Vec3{2.0f, 2.0f, 2.0f});
    const Mat4 trs = translation * rotation * scale;

    const Vec3 point = trs.TransformPoint(Vec3{1.0f, 0.0f, 0.0f});
    CG_EXPECT_TRUE(ApproxEqual(point, Vec3{1.0f, 4.0f, 3.0f}, 1.0e-5f));

    const Vec3 vector = trs.TransformVector(Vec3{1.0f, 0.0f, 0.0f});
    CG_EXPECT_TRUE(ApproxEqual(vector, Vec3{0.0f, 2.0f, 0.0f}, 1.0e-5f));

    const Vec3 composed = translation.TransformPoint(
        rotation.TransformPoint(scale.TransformPoint(Vec3{1.0f, 0.0f, 0.0f})));
    CG_EXPECT_TRUE(ApproxEqual(composed, point, 1.0e-5f));
}

CG_TEST(math_mat4_transpose_and_determinant)
{
    const Mat4 matrix{Vec4{1.0f, 2.0f, 3.0f, 4.0f}, Vec4{5.0f, 6.0f, 7.0f, 8.0f},
                      Vec4{9.0f, 10.0f, 11.0f, 12.0f}, Vec4{13.0f, 14.0f, 15.0f, 16.0f}};

    CG_EXPECT_TRUE(ApproxEqual(matrix.Transpose().Transpose(), matrix));
    CG_EXPECT_NEAR(matrix.Determinant(), 0.0f, 1.0e-4f);

    const Mat4 scale = Mat4::Scale(Vec3{2.0f, 3.0f, 4.0f});
    CG_EXPECT_NEAR(scale.Determinant(), 24.0f, 1.0e-4f);

    const Mat4 anisotropic = Mat4::Scale(Vec3{1.0e-20f, 1.0f, 1.0f});
    CG_EXPECT_NEAR(anisotropic.Determinant(), 1.0e-20f, 1.0e-26f);
}

CG_TEST(math_mat4_rotation_handedness)
{
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    const Vec3 z{0.0f, 0.0f, 1.0f};

    CG_EXPECT_TRUE(ApproxEqual(Mat4::RotationX(kHalfPi).TransformVector(y), z, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(Mat4::RotationY(kHalfPi).TransformVector(z), x, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(Mat4::RotationZ(kHalfPi).TransformVector(x), y, 1.0e-5f));
    CG_EXPECT_TRUE(ApproxEqual(Mat4::RotationAxis(Normalize(Vec3{1.0f, 2.0f, 3.0f}), 1.0f)
                                   .TransformVector(Normalize(Vec3{1.0f, 2.0f, 3.0f})),
                               Normalize(Vec3{1.0f, 2.0f, 3.0f}), 1.0e-5f));
}

CG_TEST(math_mat4_inverse_round_trip)
{
    const Mat4 translation = Mat4::Translation(Vec3{3.0f, -2.0f, 5.0f});
    const Mat4 rotation = Mat4::RotationAxis(Normalize(Vec3{1.0f, 2.0f, 3.0f}), 0.7f);
    const Mat4 scale = Mat4::Scale(Vec3{2.0f, 0.5f, 4.0f});
    const Mat4 transform = translation * rotation * scale;

    Mat4 inverse;
    CG_REQUIRE(transform.TryInverse(inverse));
    CG_EXPECT_TRUE(ApproxEqual(transform * inverse, Mat4::Identity(), 1.0e-4f));
    CG_EXPECT_TRUE(ApproxEqual(inverse * transform, Mat4::Identity(), 1.0e-4f));

    const Vec3 point{0.5f, -1.0f, 2.0f};
    const Vec3 roundTrip = inverse.TransformPoint(transform.TransformPoint(point));
    CG_EXPECT_TRUE(ApproxEqual(roundTrip, point, 1.0e-4f));

    const Mat3 upperLeft = UpperLeft3x3(transform);
    const Mat3 expectedUpperLeft = UpperLeft3x3(rotation * scale);
    CG_EXPECT_TRUE(ApproxEqual(upperLeft, expectedUpperLeft, 1.0e-4f));
}

CG_TEST(math_mat4_singular_inverse_fails_without_writing)
{
    const Mat4 singular = Mat4::Zero();
    const Mat4 untouched = Mat4::Translation(Vec3{9.0f, 9.0f, 9.0f});

    Mat4 output = untouched;
    CG_EXPECT_FALSE(singular.TryInverse(output));
    CG_EXPECT_TRUE(ApproxEqual(output, untouched));

    const Mat4 duplicated{Vec4{1.0f, 2.0f, 3.0f, 4.0f}, Vec4{1.0f, 2.0f, 3.0f, 4.0f},
                          Vec4{0.0f, 0.0f, 1.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    Mat4 duplicatedOutput = untouched;
    CG_EXPECT_FALSE(duplicated.TryInverse(duplicatedOutput));
    CG_EXPECT_TRUE(ApproxEqual(duplicatedOutput, untouched));
}

CG_TEST(math_mat4_projective_transform_divides_by_w)
{
    const Mat4 perspective = Mat4::PerspectiveReverseZ(kHalfPi, 1.0f, 0.1f, 10.0f);
    const Vec3 projected = perspective.TransformPoint(Vec3{0.0f, 0.0f, -10.0f});
    CG_EXPECT_TRUE(ApproxEqual(projected, Vec3{0.0f, 0.0f, 0.0f}, 1.0e-5f));
}

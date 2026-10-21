#include <cyber/math/color.h>
#include <cyber/test/test.h>

using namespace cyber::math;

CG_TEST(math_color_srgb_transfer)
{
    CG_EXPECT_NEAR(SrgbToLinear(0.0f), 0.0f, 1.0e-7f);
    CG_EXPECT_NEAR(SrgbToLinear(1.0f), 1.0f, 1.0e-6f);
    CG_EXPECT_NEAR(SrgbToLinear(0.5f), 0.21404114f, 1.0e-5f);
    CG_EXPECT_NEAR(SrgbToLinear(0.04045f), 0.0031308f, 1.0e-5f);

    CG_EXPECT_NEAR(LinearToSrgb(0.0f), 0.0f, 1.0e-7f);
    CG_EXPECT_NEAR(LinearToSrgb(1.0f), 1.0f, 1.0e-6f);
    CG_EXPECT_NEAR(LinearToSrgb(0.21404114f), 0.5f, 1.0e-5f);
    CG_EXPECT_NEAR(LinearToSrgb(0.0031308f), 0.04045f, 1.0e-5f);

    CG_EXPECT_NEAR(SrgbToLinear(-1.0f), 0.0f, 0.0f);
    CG_EXPECT_NEAR(SrgbToLinear(2.0f), 1.0f, 0.0f);
    CG_EXPECT_NEAR(LinearToSrgb(-1.0f), 0.0f, 0.0f);
    CG_EXPECT_NEAR(LinearToSrgb(2.0f), 1.0f, 0.0f);
}

CG_TEST(math_color_round_trip_and_alpha)
{
    const Color encoded{0.5f, 0.25f, 0.75f, 0.4f};
    const Color linear = FromSrgb(encoded);
    const Color roundTrip = ToSrgb(linear);
    CG_EXPECT_TRUE(ApproxEqual(roundTrip, encoded, 1.0e-5f));
    CG_EXPECT_NEAR(linear.a, 0.4f, 0.0f);
    CG_EXPECT_NEAR(roundTrip.a, 0.4f, 0.0f);

    const Color defaultColor;
    CG_EXPECT_NEAR(defaultColor.a, 1.0f, 0.0f);
    CG_EXPECT_NEAR(defaultColor.r, 0.0f, 0.0f);
    CG_EXPECT_NEAR(defaultColor.g, 0.0f, 0.0f);
    CG_EXPECT_NEAR(defaultColor.b, 0.0f, 0.0f);
}

#include <cyber/math/half.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstddef>
#include <limits>

using namespace cyber::math;

CG_TEST(math_half_known_values)
{
    CG_EXPECT_EQ(Half::FromFloat(0.0f).Bits(), u16{0x0000});
    CG_EXPECT_EQ(Half::FromFloat(-0.0f).Bits(), u16{0x8000});
    CG_EXPECT_EQ(Half::FromFloat(1.0f).Bits(), u16{0x3C00});
    CG_EXPECT_EQ(Half::FromFloat(-1.0f).Bits(), u16{0xBC00});
    CG_EXPECT_EQ(Half::FromFloat(0.5f).Bits(), u16{0x3800});
    CG_EXPECT_EQ(Half::FromFloat(2.0f).Bits(), u16{0x4000});

    CG_EXPECT_NEAR(Half::FromBits(0x3C00).ToFloat(), 1.0f, 0.0f);
    CG_EXPECT_NEAR(Half::FromBits(0x7BFF).ToFloat(), 65504.0f, 0.0f);
    CG_EXPECT_NEAR(Half::FromBits(0x0001).ToFloat(), std::ldexp(1.0f, -24), 0.0f);

    CG_EXPECT_TRUE(Half::FromBits(0x7C00).IsInf());
    CG_EXPECT_TRUE(Half::FromBits(0xFC00).IsInf());
    CG_EXPECT_FALSE(Half::FromBits(0x7C00).IsNaN());
    CG_EXPECT_TRUE(Half::FromBits(0x7E00).IsNaN());
    CG_EXPECT_TRUE(std::isnan(Half::FromBits(0x7E00).ToFloat()));
    CG_EXPECT_TRUE(std::isnan(Half::FromFloat(std::numeric_limits<f32>::quiet_NaN()).ToFloat()));
    CG_EXPECT_TRUE(std::isinf(Half::FromBits(0x7C00).ToFloat()));
    CG_EXPECT_TRUE(std::isinf(Half::FromBits(0xFC00).ToFloat()));
}

CG_TEST(math_half_signed_zero_and_subnormals)
{
    const Half negativeZero = Half::FromBits(0x8000);
    CG_EXPECT_TRUE(std::signbit(negativeZero.ToFloat()));
    CG_EXPECT_NEAR(negativeZero.ToFloat(), -0.0f, 0.0f);

    CG_EXPECT_NEAR(Half::FromFloat(std::ldexp(1.0f, -24)).ToFloat(), std::ldexp(1.0f, -24), 0.0f);
    CG_EXPECT_EQ(Half::FromFloat(std::ldexp(1.0f, -24)).Bits(), u16{0x0001});
    CG_EXPECT_EQ(Half::FromFloat(std::ldexp(1.0f, -25)).Bits(), u16{0x0000});
    CG_EXPECT_EQ(Half::FromFloat(65520.0f).Bits(), u16{0x7C00});
    CG_EXPECT_EQ(Half::FromFloat(65519.0f).Bits(), u16{0x7BFF});
}

CG_TEST(math_half_exhaustive_round_trip)
{
    std::size_t mismatches = 0;
    for (u32 pattern = 0; pattern < 0x10000u; ++pattern)
    {
        const Half value = Half::FromBits(static_cast<u16>(pattern));
        const f32 converted = value.ToFloat();
        if (value.IsNaN())
        {
            if (!std::isnan(converted))
            {
                ++mismatches;
            }
            continue;
        }

        const Half roundTrip = Half::FromFloat(converted);
        if (roundTrip.Bits() != static_cast<u16>(pattern))
        {
            ++mismatches;
        }
    }
    CG_EXPECT_EQ(mismatches, std::size_t{0});
}

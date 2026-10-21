#include <cyber/math/aabb.h>
#include <cyber/math/color.h>
#include <cyber/math/constants.h>
#include <cyber/math/frustum.h>
#include <cyber/math/half.h>
#include <cyber/math/mat3.h>
#include <cyber/math/mat4.h>
#include <cyber/math/math.h>
#include <cyber/math/plane.h>
#include <cyber/math/quat.h>
#include <cyber/math/ray.h>
#include <cyber/math/simd.h>
#include <cyber/math/sphere.h>
#include <cyber/math/transform.h>
#include <cyber/math/types.h>
#include <cyber/math/vec2.h>
#include <cyber/math/vec3.h>
#include <cyber/math/vec4.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::math;

static_assert(std::is_standard_layout_v<Vec2>);
static_assert(std::is_standard_layout_v<Vec3>);
static_assert(std::is_standard_layout_v<Vec4>);
static_assert(std::is_standard_layout_v<Mat3>);
static_assert(std::is_standard_layout_v<Mat4>);
static_assert(std::is_standard_layout_v<Quat>);

static_assert(sizeof(Vec2) == 8);
static_assert(sizeof(Vec3) == 12);
static_assert(sizeof(Vec4) == 16);
static_assert(sizeof(Mat3) == 36);
static_assert(sizeof(Mat4) == 64);
static_assert(sizeof(Quat) == 16);
static_assert(sizeof(Color) == 16);
static_assert(sizeof(Half) == 2);

static_assert(alignof(Vec2) == 4);
static_assert(alignof(Vec4) == 16);
static_assert(alignof(Mat4) == 16);

CG_TEST(math_public_headers_compile_together)
{
    CG_EXPECT_NEAR(Radians(180.0f), kPi, 1.0e-5f);
    CG_EXPECT_NEAR(Degrees(kPi), 180.0f, 1.0e-5f);

    const Color color;
    CG_EXPECT_NEAR(color.a, 1.0f, 0.0f);

    const Half half;
    CG_EXPECT_EQ(half.Bits(), u16{0});

    const Aabb box = Aabb::FromMinMax(Vec3::Zero(), Vec3::One());
    CG_EXPECT_TRUE(box.Contains(Vec3{0.5f, 0.5f, 0.5f}));

    CG_EXPECT_TRUE(ActiveSimdBackend() == SimdBackend::Sse2 ||
                   ActiveSimdBackend() == SimdBackend::Scalar);
}

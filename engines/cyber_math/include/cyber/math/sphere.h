#pragma once

#include <cyber/math/vec3.h>

namespace cyber::math
{

struct Sphere
{
    Vec3 center{};
    f32 radius = 0.0f;

    [[nodiscard]] constexpr bool Contains(Vec3 point) const noexcept
    {
        return LengthSquared(point - center) <= radius * radius;
    }
};

}  // namespace cyber::math

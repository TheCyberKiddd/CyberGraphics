#pragma once

#include <cyber/math/constants.h>
#include <cyber/math/vec3.h>

namespace cyber::math
{

// Plane equation n . p + d = 0. Geometry code expects a normalized normal.
struct Plane
{
    Vec3 normal{0.0f, 1.0f, 0.0f};
    f32 distance = 0.0f;
};

[[nodiscard]] inline Plane FromPointNormal(Vec3 point, Vec3 normal) noexcept
{
    const Vec3 normalized = Normalize(normal);
    return Plane{normalized, -Dot(normalized, point)};
}

[[nodiscard]] constexpr f32 DistanceTo(const Plane& plane, Vec3 point) noexcept
{
    return Dot(plane.normal, point) + plane.distance;
}

[[nodiscard]] inline Plane Normalize(const Plane& plane) noexcept
{
    const f32 length = Length(plane.normal);
    if (length <= 0.0f)
    {
        return plane;
    }
    return Plane{plane.normal / length, plane.distance / length};
}

[[nodiscard]] inline bool ApproxEqual(const Plane& lhs, const Plane& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.normal, rhs.normal, tolerance) &&
           ApproxEqual(lhs.distance, rhs.distance, tolerance);
}

}  // namespace cyber::math

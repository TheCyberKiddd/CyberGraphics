#pragma once

#include <cyber/math/aabb.h>
#include <cyber/math/sphere.h>
#include <cyber/math/vec3.h>

namespace cyber::math
{

// origin + t * direction, with t >= 0. Directions are normalized unless a
// caller explicitly documents another unit.
struct Ray
{
    Vec3 origin{};
    Vec3 direction{0.0f, 0.0f, -1.0f};

    [[nodiscard]] constexpr Vec3 PointAt(f32 distance) const noexcept
    {
        return origin + direction * distance;
    }
};

// Returns the entry distance in `hitDistance` when the ray starts outside. When
// the origin is inside, the entry distance is zero. The output is untouched on
// a miss.
[[nodiscard]] bool Intersect(const Ray& ray, const Aabb& box, f32* hitDistance = nullptr) noexcept;

// Returns the nearest non-negative intersection distance. When the origin is
// inside the sphere this is the exit distance.
[[nodiscard]] bool Intersect(const Ray& ray, const Sphere& sphere,
                             f32* hitDistance = nullptr) noexcept;

}  // namespace cyber::math

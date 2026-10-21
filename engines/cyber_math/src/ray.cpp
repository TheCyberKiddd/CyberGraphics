#include <cyber/math/ray.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cyber::math
{

bool Intersect(const Ray& ray, const Aabb& box, f32* hitDistance) noexcept
{
    if (box.IsEmpty())
    {
        return false;
    }

    const Vec3 minimum = box.Min();
    const Vec3 maximum = box.Max();
    f32 nearDistance = 0.0f;
    f32 farDistance = std::numeric_limits<f32>::infinity();

    for (usize axis = 0; axis < 3; ++axis)
    {
        const f32 origin = ray.origin[axis];
        const f32 direction = ray.direction[axis];

        if (std::abs(direction) < 1.0e-8f)
        {
            if (origin < minimum[axis] || origin > maximum[axis])
            {
                return false;
            }
            continue;
        }

        const f32 inverseDirection = 1.0f / direction;
        f32 first = (minimum[axis] - origin) * inverseDirection;
        f32 second = (maximum[axis] - origin) * inverseDirection;
        if (first > second)
        {
            std::swap(first, second);
        }

        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (nearDistance > farDistance)
        {
            return false;
        }
    }

    if (farDistance < 0.0f)
    {
        return false;
    }

    if (hitDistance != nullptr)
    {
        *hitDistance = nearDistance;
    }
    return true;
}

bool Intersect(const Ray& ray, const Sphere& sphere, f32* hitDistance) noexcept
{
    const Vec3 offset = ray.origin - sphere.center;
    const f32 a = Dot(ray.direction, ray.direction);
    if (a <= 0.0f)
    {
        return false;
    }

    const f32 b = 2.0f * Dot(offset, ray.direction);
    const f32 c = Dot(offset, offset) - sphere.radius * sphere.radius;
    const f32 discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f)
    {
        return false;
    }

    const f32 root = std::sqrt(discriminant);
    const f32 nearDistance = (-b - root) / (2.0f * a);
    const f32 farDistance = (-b + root) / (2.0f * a);

    if (nearDistance >= 0.0f)
    {
        if (hitDistance != nullptr)
        {
            *hitDistance = nearDistance;
        }
        return true;
    }
    if (farDistance >= 0.0f)
    {
        if (hitDistance != nullptr)
        {
            *hitDistance = farDistance;
        }
        return true;
    }
    return false;
}

}  // namespace cyber::math

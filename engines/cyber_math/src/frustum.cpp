#include <cyber/math/frustum.h>
#include <cyber/math/vec4.h>

namespace cyber::math
{
namespace
{

[[nodiscard]] Plane MakePlane(Vec4 coefficients) noexcept
{
    return Normalize(Plane{Vec3{coefficients.x, coefficients.y, coefficients.z}, coefficients.w});
}

}  // namespace

Frustum Frustum::FromViewProjection(const Mat4& viewProjection) noexcept
{
    const auto row = [&viewProjection](usize index) noexcept
    {
        return Vec4{viewProjection[0][index], viewProjection[1][index], viewProjection[2][index],
                    viewProjection[3][index]};
    };

    const Vec4 row0 = row(0);
    const Vec4 row1 = row(1);
    const Vec4 row2 = row(2);
    const Vec4 row3 = row(3);

    Frustum result;
    result[FrustumPlane::Left] = MakePlane(row3 + row0);
    result[FrustumPlane::Right] = MakePlane(row3 - row0);
    result[FrustumPlane::Bottom] = MakePlane(row3 + row1);
    result[FrustumPlane::Top] = MakePlane(row3 - row1);
    result[FrustumPlane::Near] = MakePlane(row3 - row2);
    result[FrustumPlane::Far] = MakePlane(row2);
    return result;
}

bool Contains(const Frustum& frustum, Vec3 point) noexcept
{
    for (const Plane& plane : frustum.planes)
    {
        if (DistanceTo(plane, point) < 0.0f)
        {
            return false;
        }
    }
    return true;
}

bool Intersects(const Frustum& frustum, const Aabb& box) noexcept
{
    if (box.IsEmpty())
    {
        return false;
    }

    const Vec3 minimum = box.Min();
    const Vec3 maximum = box.Max();

    for (const Plane& plane : frustum.planes)
    {
        const Vec3 positiveVertex{plane.normal.x >= 0.0f ? maximum.x : minimum.x,
                                  plane.normal.y >= 0.0f ? maximum.y : minimum.y,
                                  plane.normal.z >= 0.0f ? maximum.z : minimum.z};
        if (DistanceTo(plane, positiveVertex) < 0.0f)
        {
            return false;
        }
    }
    return true;
}

}  // namespace cyber::math

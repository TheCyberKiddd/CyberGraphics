#pragma once

#include <cyber/math/aabb.h>
#include <cyber/math/mat4.h>
#include <cyber/math/plane.h>
#include <cyber/math/types.h>

namespace cyber::math
{

enum class FrustumPlane : u8
{
    Left = 0,
    Right = 1,
    Bottom = 2,
    Top = 3,
    Near = 4,
    Far = 5,
};

// Six outward-facing planes. A point is inside when every plane distance is
// non-negative.
struct Frustum
{
    Plane planes[6];

    [[nodiscard]] constexpr Plane& operator[](FrustumPlane index) noexcept
    {
        return planes[static_cast<usize>(index)];
    }

    [[nodiscard]] constexpr const Plane& operator[](FrustumPlane index) const noexcept
    {
        return planes[static_cast<usize>(index)];
    }

    // Extracts and normalizes the six planes from a view-projection matrix that
    // uses a 0..1 reverse-Z clip depth.
    [[nodiscard]] static Frustum FromViewProjection(const Mat4& viewProjection) noexcept;
};

[[nodiscard]] bool Contains(const Frustum& frustum, Vec3 point) noexcept;

// Conservative positive-vertex test. A box that straddles a plane is reported
// as intersecting.
[[nodiscard]] bool Intersects(const Frustum& frustum, const Aabb& box) noexcept;

}  // namespace cyber::math

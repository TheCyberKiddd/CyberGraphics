#pragma once

#include <cyber/math/vec3.h>

namespace cyber::math
{

// Inclusive axis-aligned bounding box. Empty is an explicit state; it is never
// encoded with inverted infinities. Use the factories or Expand() to keep the
// state consistent.
class Aabb
{
public:
    constexpr Aabb() noexcept = default;

    [[nodiscard]] static constexpr Aabb FromMinMax(Vec3 minValue, Vec3 maxValue) noexcept
    {
        Aabb box;
        box.m_min = minValue;
        box.m_max = maxValue;
        box.m_empty = minValue.x > maxValue.x || minValue.y > maxValue.y || minValue.z > maxValue.z;
        return box;
    }

    [[nodiscard]] static constexpr Aabb FromCenterExtents(Vec3 center, Vec3 extents) noexcept
    {
        if (extents.x < 0.0f || extents.y < 0.0f || extents.z < 0.0f)
        {
            return Aabb{};
        }
        return FromMinMax(center - extents, center + extents);
    }

    [[nodiscard]] static constexpr Aabb Empty() noexcept
    {
        return Aabb{};
    }

    [[nodiscard]] constexpr bool IsEmpty() const noexcept
    {
        return m_empty;
    }

    [[nodiscard]] constexpr Vec3 Min() const noexcept
    {
        return m_min;
    }

    [[nodiscard]] constexpr Vec3 Max() const noexcept
    {
        return m_max;
    }

    [[nodiscard]] constexpr Vec3 Center() const noexcept
    {
        return m_empty ? Vec3::Zero() : (m_min + m_max) * 0.5f;
    }

    [[nodiscard]] constexpr Vec3 Extents() const noexcept
    {
        return m_empty ? Vec3::Zero() : (m_max - m_min) * 0.5f;
    }

    constexpr void Expand(Vec3 point) noexcept
    {
        if (m_empty)
        {
            m_min = point;
            m_max = point;
            m_empty = false;
            return;
        }
        m_min = cyber::math::Min(m_min, point);
        m_max = cyber::math::Max(m_max, point);
    }

    constexpr void Expand(const Aabb& other) noexcept
    {
        if (other.m_empty)
        {
            return;
        }
        Expand(other.m_min);
        Expand(other.m_max);
    }

    [[nodiscard]] constexpr Aabb Expanded(Vec3 point) const noexcept
    {
        Aabb result = *this;
        result.Expand(point);
        return result;
    }

    [[nodiscard]] constexpr bool Contains(Vec3 point) const noexcept
    {
        return !m_empty && point.x >= m_min.x && point.x <= m_max.x && point.y >= m_min.y &&
               point.y <= m_max.y && point.z >= m_min.z && point.z <= m_max.z;
    }

    [[nodiscard]] constexpr bool Overlaps(const Aabb& other) const noexcept
    {
        return !m_empty && !other.m_empty && m_min.x <= other.m_max.x && m_max.x >= other.m_min.x &&
               m_min.y <= other.m_max.y && m_max.y >= other.m_min.y && m_min.z <= other.m_max.z &&
               m_max.z >= other.m_min.z;
    }

private:
    Vec3 m_min{};
    Vec3 m_max{};
    bool m_empty = true;
};

[[nodiscard]] inline bool ApproxEqual(const Aabb& lhs, const Aabb& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    if (lhs.IsEmpty() || rhs.IsEmpty())
    {
        return lhs.IsEmpty() == rhs.IsEmpty();
    }
    return ApproxEqual(lhs.Min(), rhs.Min(), tolerance) &&
           ApproxEqual(lhs.Max(), rhs.Max(), tolerance);
}

}  // namespace cyber::math

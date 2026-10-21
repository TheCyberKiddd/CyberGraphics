#include <cyber/math/quat.h>

#include <cmath>

namespace cyber::math
{

Quat Quat::FromEuler(f32 yawRadians, f32 pitchRadians, f32 rollRadians) noexcept
{
    const Quat yaw = FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yawRadians);
    const Quat pitch = FromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitchRadians);
    const Quat roll = FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, rollRadians);
    return (yaw * pitch * roll).Normalize();
}

Quat Quat::FromMat3(const Mat3& matrix) noexcept
{
    const f32 trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
    Quat result;

    if (trace > 0.0f)
    {
        const f32 scale = std::sqrt(trace + 1.0f) * 2.0f;
        result.w = 0.25f * scale;
        result.x = (matrix[1][2] - matrix[2][1]) / scale;
        result.y = (matrix[2][0] - matrix[0][2]) / scale;
        result.z = (matrix[0][1] - matrix[1][0]) / scale;
    }
    else if (matrix[0][0] > matrix[1][1] && matrix[0][0] > matrix[2][2])
    {
        const f32 scale = std::sqrt(1.0f + matrix[0][0] - matrix[1][1] - matrix[2][2]) * 2.0f;
        result.w = (matrix[1][2] - matrix[2][1]) / scale;
        result.x = 0.25f * scale;
        result.y = (matrix[1][0] + matrix[0][1]) / scale;
        result.z = (matrix[2][0] + matrix[0][2]) / scale;
    }
    else if (matrix[1][1] > matrix[2][2])
    {
        const f32 scale = std::sqrt(1.0f + matrix[1][1] - matrix[0][0] - matrix[2][2]) * 2.0f;
        result.w = (matrix[2][0] - matrix[0][2]) / scale;
        result.x = (matrix[1][0] + matrix[0][1]) / scale;
        result.y = 0.25f * scale;
        result.z = (matrix[2][1] + matrix[1][2]) / scale;
    }
    else
    {
        const f32 scale = std::sqrt(1.0f + matrix[2][2] - matrix[0][0] - matrix[1][1]) * 2.0f;
        result.w = (matrix[0][1] - matrix[1][0]) / scale;
        result.x = (matrix[2][0] + matrix[0][2]) / scale;
        result.y = (matrix[2][1] + matrix[1][2]) / scale;
        result.z = 0.25f * scale;
    }

    return result.Normalize();
}

Quat Quat::FromMat4(const Mat4& matrix) noexcept
{
    return FromMat3(UpperLeft3x3(matrix));
}

Quat Quat::FromTo(Vec3 from, Vec3 to) noexcept
{
    const Vec3 normalizedFrom = cyber::math::Normalize(from);
    const Vec3 normalizedTo = cyber::math::Normalize(to);
    if (cyber::math::LengthSquared(normalizedFrom) == 0.0f ||
        cyber::math::LengthSquared(normalizedTo) == 0.0f)
    {
        return Identity();
    }

    const f32 alignment = cyber::math::Dot(normalizedFrom, normalizedTo);
    if (alignment >= 1.0f - kEpsilon)
    {
        return Identity();
    }
    if (alignment <= -1.0f + kEpsilon)
    {
        Vec3 axis = cyber::math::Cross(normalizedFrom, Vec3{1.0f, 0.0f, 0.0f});
        if (cyber::math::LengthSquared(axis) < kEpsilon)
        {
            axis = cyber::math::Cross(normalizedFrom, Vec3{0.0f, 1.0f, 0.0f});
        }
        return FromAxisAngle(axis, kPi);
    }

    const Vec3 axis = cyber::math::Cross(normalizedFrom, normalizedTo);
    return Quat{axis.x, axis.y, axis.z, 1.0f + alignment}.Normalize();
}

Quat Quat::Nlerp(const Quat& from, const Quat& to, f32 factor) noexcept
{
    const Quat target = from.Dot(to) < 0.0f ? Quat{-to.x, -to.y, -to.z, -to.w} : to;
    return Quat{from.x + (target.x - from.x) * factor, from.y + (target.y - from.y) * factor,
                from.z + (target.z - from.z) * factor, from.w + (target.w - from.w) * factor}
        .Normalize();
}

Quat Quat::Slerp(const Quat& from, const Quat& to, f32 factor) noexcept
{
    Quat target = to;
    f32 alignment = from.Dot(target);
    if (alignment < 0.0f)
    {
        target = Quat{-target.x, -target.y, -target.z, -target.w};
        alignment = -alignment;
    }

    if (alignment > 0.9995f)
    {
        return Nlerp(from, target, factor);
    }

    alignment = Clamp(alignment, -1.0f, 1.0f);
    const f32 angle = std::acos(alignment);
    const f32 sine = std::sin(angle);
    const f32 fromWeight = std::sin((1.0f - factor) * angle) / sine;
    const f32 toWeight = std::sin(factor * angle) / sine;
    return Quat{
        from.x * fromWeight + target.x * toWeight, from.y * fromWeight + target.y * toWeight,
        from.z * fromWeight + target.z * toWeight, from.w * fromWeight + target.w * toWeight}
        .Normalize();
}

}  // namespace cyber::math

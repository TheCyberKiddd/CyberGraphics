#include <cyber/math/mat4.h>

#include <algorithm>
#include <cmath>

namespace cyber::math
{

Mat4 Mat4::RotationX(f32 radians) noexcept
{
    const f32 cosine = std::cos(radians);
    const f32 sine = std::sin(radians);
    return Mat4{Vec4{1.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, cosine, sine, 0.0f},
                Vec4{0.0f, -sine, cosine, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

Mat4 Mat4::RotationY(f32 radians) noexcept
{
    const f32 cosine = std::cos(radians);
    const f32 sine = std::sin(radians);
    return Mat4{Vec4{cosine, 0.0f, -sine, 0.0f}, Vec4{0.0f, 1.0f, 0.0f, 0.0f},
                Vec4{sine, 0.0f, cosine, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

Mat4 Mat4::RotationZ(f32 radians) noexcept
{
    const f32 cosine = std::cos(radians);
    const f32 sine = std::sin(radians);
    return Mat4{Vec4{cosine, sine, 0.0f, 0.0f}, Vec4{-sine, cosine, 0.0f, 0.0f},
                Vec4{0.0f, 0.0f, 1.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

Mat4 Mat4::RotationAxis(Vec3 axis, f32 radians) noexcept
{
    const Vec3 normalized = Normalize(axis);
    if (LengthSquared(normalized) == 0.0f)
    {
        return Identity();
    }

    const f32 cosine = std::cos(radians);
    const f32 sine = std::sin(radians);
    const f32 oneMinusCosine = 1.0f - cosine;
    const f32 x = normalized.x;
    const f32 y = normalized.y;
    const f32 z = normalized.z;

    return Mat4{Vec4{oneMinusCosine * x * x + cosine, oneMinusCosine * x * y + sine * z,
                     oneMinusCosine * x * z - sine * y, 0.0f},
                Vec4{oneMinusCosine * x * y - sine * z, oneMinusCosine * y * y + cosine,
                     oneMinusCosine * y * z + sine * x, 0.0f},
                Vec4{oneMinusCosine * x * z + sine * y, oneMinusCosine * y * z - sine * x,
                     oneMinusCosine * z * z + cosine, 0.0f},
                Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

Mat4 Mat4::LookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept
{
    const Vec3 forward = Normalize(target - eye);
    if (LengthSquared(forward) == 0.0f)
    {
        return Identity();
    }

    const Vec3 right = Normalize(Cross(forward, up));
    if (LengthSquared(right) == 0.0f)
    {
        return Identity();
    }

    const Vec3 correctedUp = Cross(right, forward);
    return Mat4{Vec4{right.x, correctedUp.x, -forward.x, 0.0f},
                Vec4{right.y, correctedUp.y, -forward.y, 0.0f},
                Vec4{right.z, correctedUp.z, -forward.z, 0.0f},
                Vec4{-Dot(right, eye), -Dot(correctedUp, eye), Dot(forward, eye), 1.0f}};
}

Mat4 Mat4::PerspectiveReverseZ(f32 fovYRadians, f32 aspect, f32 nearPlane, f32 farPlane) noexcept
{
    const f32 focal = 1.0f / std::tan(fovYRadians * 0.5f);
    const f32 depthRange = farPlane - nearPlane;
    return Mat4{Vec4{focal / aspect, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, focal, 0.0f, 0.0f},
                Vec4{0.0f, 0.0f, nearPlane / depthRange, -1.0f},
                Vec4{0.0f, 0.0f, nearPlane * farPlane / depthRange, 0.0f}};
}

Mat4 Mat4::PerspectiveInfiniteReverseZ(f32 fovYRadians, f32 aspect, f32 nearPlane) noexcept
{
    const f32 focal = 1.0f / std::tan(fovYRadians * 0.5f);
    return Mat4{Vec4{focal / aspect, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, focal, 0.0f, 0.0f},
                Vec4{0.0f, 0.0f, 0.0f, -1.0f}, Vec4{0.0f, 0.0f, nearPlane, 0.0f}};
}

Mat4 Mat4::OrthographicReverseZ(f32 left, f32 right, f32 bottom, f32 top, f32 nearPlane,
                                f32 farPlane) noexcept
{
    const f32 width = right - left;
    const f32 height = top - bottom;
    const f32 depthRange = farPlane - nearPlane;
    return Mat4{
        Vec4{2.0f / width, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 2.0f / height, 0.0f, 0.0f},
        Vec4{0.0f, 0.0f, 1.0f / depthRange, 0.0f},
        Vec4{-(right + left) / width, -(top + bottom) / height, farPlane / depthRange, 1.0f}};
}

f32 Mat4::Determinant() const noexcept
{
    f64 rows[4][4]{};
    f64 maxAbs = 0.0;
    for (usize row = 0; row < 4; ++row)
    {
        for (usize column = 0; column < 4; ++column)
        {
            const f64 value = static_cast<f64>(columns[column][row]);
            rows[row][column] = value;
            maxAbs = std::max(maxAbs, std::abs(value));
        }
    }

    if (maxAbs == 0.0)
    {
        return 0.0f;
    }

    f64 determinant = 1.0;

    for (usize column = 0; column < 4; ++column)
    {
        usize pivotRow = column;
        f64 pivotMagnitude = std::abs(rows[column][column]);
        for (usize row = column + 1; row < 4; ++row)
        {
            const f64 magnitude = std::abs(rows[row][column]);
            if (magnitude > pivotMagnitude)
            {
                pivotMagnitude = magnitude;
                pivotRow = row;
            }
        }

        if (pivotMagnitude == 0.0)
        {
            return 0.0f;
        }

        if (pivotRow != column)
        {
            for (usize index = 0; index < 4; ++index)
            {
                std::swap(rows[column][index], rows[pivotRow][index]);
            }
            determinant = -determinant;
        }

        const f64 pivot = rows[column][column];
        determinant *= pivot;

        for (usize row = column + 1; row < 4; ++row)
        {
            const f64 factor = rows[row][column] / pivot;
            for (usize index = column; index < 4; ++index)
            {
                rows[row][index] -= factor * rows[column][index];
            }
        }
    }

    return static_cast<f32>(determinant);
}

bool Mat4::TryInverse(Mat4& out) const noexcept
{
    f64 augmented[4][8]{};
    f64 maxAbs = 0.0;

    for (usize row = 0; row < 4; ++row)
    {
        for (usize column = 0; column < 4; ++column)
        {
            const f64 value = static_cast<f64>(columns[column][row]);
            augmented[row][column] = value;
            maxAbs = std::max(maxAbs, std::abs(value));
        }
        augmented[row][4 + row] = 1.0;
    }

    if (maxAbs == 0.0)
    {
        return false;
    }

    const f64 threshold = maxAbs * 1.0e-12;

    for (usize column = 0; column < 4; ++column)
    {
        usize pivotRow = column;
        f64 pivotMagnitude = std::abs(augmented[column][column]);
        for (usize row = column + 1; row < 4; ++row)
        {
            const f64 magnitude = std::abs(augmented[row][column]);
            if (magnitude > pivotMagnitude)
            {
                pivotMagnitude = magnitude;
                pivotRow = row;
            }
        }

        if (pivotMagnitude <= threshold)
        {
            return false;
        }

        if (pivotRow != column)
        {
            for (usize index = 0; index < 8; ++index)
            {
                std::swap(augmented[column][index], augmented[pivotRow][index]);
            }
        }

        const f64 inversePivot = 1.0 / augmented[column][column];
        for (usize index = 0; index < 8; ++index)
        {
            augmented[column][index] *= inversePivot;
        }

        for (usize row = 0; row < 4; ++row)
        {
            if (row == column)
            {
                continue;
            }
            const f64 factor = augmented[row][column];
            for (usize index = 0; index < 8; ++index)
            {
                augmented[row][index] -= factor * augmented[column][index];
            }
        }
    }

    Mat4 result;
    for (usize row = 0; row < 4; ++row)
    {
        for (usize column = 0; column < 4; ++column)
        {
            result.columns[column][row] = static_cast<f32>(augmented[row][4 + column]);
        }
    }

    out = result;
    return true;
}

Mat3 UpperLeft3x3(const Mat4& matrix) noexcept
{
    return Mat3{Vec3{matrix[0].x, matrix[0].y, matrix[0].z},
                Vec3{matrix[1].x, matrix[1].y, matrix[1].z},
                Vec3{matrix[2].x, matrix[2].y, matrix[2].z}};
}

}  // namespace cyber::math

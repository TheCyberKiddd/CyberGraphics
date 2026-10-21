#include <cyber/math/mat3.h>

#include <algorithm>
#include <cmath>

namespace cyber::math
{

bool Mat3::TryInverse(Mat3& out) const noexcept
{
    f64 augmented[3][6]{};
    f64 maxAbs = 0.0;

    for (usize row = 0; row < 3; ++row)
    {
        for (usize column = 0; column < 3; ++column)
        {
            const f64 value = static_cast<f64>(columns[column][row]);
            augmented[row][column] = value;
            maxAbs = std::max(maxAbs, std::abs(value));
        }
        augmented[row][3 + row] = 1.0;
    }

    if (maxAbs == 0.0)
    {
        return false;
    }

    const f64 threshold = maxAbs * 1.0e-12;

    for (usize column = 0; column < 3; ++column)
    {
        usize pivotRow = column;
        f64 pivotMagnitude = std::abs(augmented[column][column]);
        for (usize row = column + 1; row < 3; ++row)
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
            for (usize index = 0; index < 6; ++index)
            {
                std::swap(augmented[column][index], augmented[pivotRow][index]);
            }
        }

        const f64 inversePivot = 1.0 / augmented[column][column];
        for (usize index = 0; index < 6; ++index)
        {
            augmented[column][index] *= inversePivot;
        }

        for (usize row = 0; row < 3; ++row)
        {
            if (row == column)
            {
                continue;
            }
            const f64 factor = augmented[row][column];
            for (usize index = 0; index < 6; ++index)
            {
                augmented[row][index] -= factor * augmented[column][index];
            }
        }
    }

    Mat3 result;
    for (usize row = 0; row < 3; ++row)
    {
        for (usize column = 0; column < 3; ++column)
        {
            result.columns[column][row] = static_cast<f32>(augmented[row][3 + column]);
        }
    }

    out = result;
    return true;
}

}  // namespace cyber::math

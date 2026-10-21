#include <cyber/math/transform.h>

namespace cyber::math
{

bool Transform::TryInverse(Mat4& out) const noexcept
{
    return ToMatrix().TryInverse(out);
}

}  // namespace cyber::math

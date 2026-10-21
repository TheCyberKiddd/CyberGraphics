#include <cyber/core/build_config.h>
#include <cyber/core/error.h>
#include <cyber/core/hash.h>
#include <cyber/core/log.h>
#include <cyber/core/result.h>
#include <cyber/core/version.h>
#include <cyber/math/math.h>

#include <cmath>
#include <cstring>
#include <string_view>

int main()
{
    using namespace cyber::core;

    const Version version = VersionNumber();
    if (version.major != kVersionMajor)
    {
        return 1;
    }

    const char* text = VersionString();
    if (text == nullptr)
    {
        return 2;
    }

    if (std::strcmp(text, kVersionString) != 0)
    {
        return 3;
    }

    if (BuildConfig::kVersion != std::string_view(kVersionString))
    {
        return 4;
    }

    auto result = Result<int, Error>::Ok(21);
    if (!result.IsOk() || result.Value() != 21)
    {
        return 5;
    }

    auto failed = Result<void, Error>::Err(Error{ErrorCode::NotFound, "missing"});
    if (!failed.IsError() || failed.Error().code != ErrorCode::NotFound)
    {
        return 6;
    }

    if (HashString("abc") != 0x44BC2CF5AD770999ULL)
    {
        return 7;
    }

    Logger logger;
    logger.SetLevel(LogLevel::Info);
    if (!logger.IsEnabled(LogChannel::Core, LogLevel::Info))
    {
        return 8;
    }

    using namespace cyber::math;

    const Vec3 cross = Cross(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    if (LengthSquared(cross - Vec3{0.0f, 0.0f, 1.0f}) > 1.0e-6f)
    {
        return 9;
    }

    const Mat4 transform = Mat4::Translation(Vec3{1.0f, 2.0f, 3.0f}) * Mat4::RotationZ(kHalfPi) *
                           Mat4::Scale(Vec3{2.0f, 2.0f, 2.0f});
    Mat4 inverse;
    if (!transform.TryInverse(inverse))
    {
        return 10;
    }
    const Vec3 origin = Vec3::Zero();
    if (LengthSquared(inverse.TransformPoint(transform.TransformPoint(origin)) - origin) > 1.0e-5f)
    {
        return 11;
    }

    const Quat rotation = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
    if (!ApproxEqual(rotation.Rotate(Vec3{1.0f, 0.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}, 1.0e-5f))
    {
        return 12;
    }

    if (Half::FromFloat(0.5f).Bits() != static_cast<u16>(0x3800))
    {
        return 13;
    }

    const Mat4 projection = Mat4::PerspectiveReverseZ(kHalfPi, 1.0f, 0.1f, 10.0f);
    if (std::abs(projection.TransformPoint(Vec3{0.0f, 0.0f, -0.1f}).z - 1.0f) > 1.0e-5f)
    {
        return 14;
    }

    const Aabb box = Aabb::FromMinMax(Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f});
    if (!box.Contains(Vec3::Zero()))
    {
        return 15;
    }

    return 0;
}

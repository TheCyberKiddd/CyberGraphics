#include <cyber/assets/material.h>

#include <cmath>

namespace cyber::assets
{
namespace
{

[[nodiscard]] bool InRange(f32 value, f32 minimum, f32 maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

}  // namespace

ImportStatus MaterialAsset::Validate() const noexcept
{
    const f32 colorComponents[4] = {baseColorFactor.x, baseColorFactor.y, baseColorFactor.z,
                                    baseColorFactor.w};
    for (f32 component : colorComponents)
    {
        if (!InRange(component, 0.0f, 1.0f))
        {
            return ImportStatus::Err(MakeError(ErrorCode::InvalidArgument,
                                               "material base color factor is out of range"));
        }
    }
    const f32 emissive[3] = {emissiveFactor.x, emissiveFactor.y, emissiveFactor.z};
    for (f32 component : emissive)
    {
        if (!InRange(component, 0.0f, 1.0f))
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::InvalidArgument, "material emissive factor is out of range"));
        }
    }
    if (!InRange(metallicFactor, 0.0f, 1.0f) || !InRange(roughnessFactor, 0.0f, 1.0f))
    {
        return ImportStatus::Err(MakeError(
            ErrorCode::InvalidArgument, "material metallic or roughness factor is out of range"));
    }
    if (!InRange(normalScale, 0.0f, 100.0f) || !InRange(occlusionStrength, 0.0f, 1.0f))
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "material texture scale is out of range"));
    }
    if (alphaMode == AlphaMode::Mask && !InRange(alphaCutoff, 0.0f, 1.0f))
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "material alpha cutoff is out of range"));
    }
    if (alphaMode != AlphaMode::Opaque && alphaMode != AlphaMode::Mask &&
        alphaMode != AlphaMode::Blend)
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "material alpha mode is invalid"));
    }
    return ImportStatus::Ok();
}

}  // namespace cyber::assets

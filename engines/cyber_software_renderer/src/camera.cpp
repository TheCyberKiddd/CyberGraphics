#include <cyber/software/camera.h>

#include <cmath>

namespace cyber::software
{

using cyber::core::f32;
using cyber::core::u32;

namespace
{

// Keeps the view basis away from the poles, where LookAt's up vector becomes
// parallel to the view direction.
constexpr f32 kMaxPitch = cyber::math::kHalfPi - 1.0e-3f;
constexpr f32 kMaxFlyDeltaSeconds = 0.25f;
constexpr cyber::math::Vec3 kWorldUp{0.0f, 1.0f, 0.0f};

cyber::math::Vec3 HorizontalRight(f32 yawRadians) noexcept
{
    // Right is the forward direction rotated -90 degrees around +Y.
    return cyber::math::Vec3{std::cos(yawRadians), 0.0f, std::sin(yawRadians)};
}

}  // namespace

cyber::math::Vec3 CameraForward(const Camera& camera) noexcept
{
    const f32 cosPitch = std::cos(camera.pitchRadians);
    return cyber::math::Vec3{cosPitch * std::sin(camera.yawRadians), std::sin(camera.pitchRadians),
                             -cosPitch * std::cos(camera.yawRadians)};
}

cyber::math::Vec3 CameraRight(const Camera& camera) noexcept
{
    return HorizontalRight(camera.yawRadians);
}

cyber::math::Vec3 CameraUp(const Camera& camera) noexcept
{
    return cyber::math::Cross(CameraRight(camera), CameraForward(camera));
}

cyber::math::Mat4 CameraView(const Camera& camera) noexcept
{
    return cyber::math::Mat4::LookAt(camera.position, camera.position + CameraForward(camera),
                                     kWorldUp);
}

cyber::math::Mat4 CameraProjection(const Camera& camera, f32 aspect) noexcept
{
    if (!std::isfinite(aspect) || aspect <= 0.0f || !std::isfinite(camera.fovYRadians) ||
        camera.fovYRadians <= 0.0f || camera.fovYRadians >= cyber::math::kPi ||
        !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) ||
        camera.nearPlane <= 0.0f || camera.farPlane <= camera.nearPlane)
    {
        return cyber::math::Mat4::Identity();
    }
    return cyber::math::Mat4::PerspectiveReverseZ(camera.fovYRadians, aspect, camera.nearPlane,
                                                  camera.farPlane);
}

void UpdateFlyCamera(FlyCamera& fly, const FlyCameraInput& input, f32 deltaSeconds) noexcept
{
    Camera& camera = fly.camera;
    camera.yawRadians += input.yawDelta;
    camera.pitchRadians =
        cyber::math::Clamp(camera.pitchRadians + input.pitchDelta, -kMaxPitch, kMaxPitch);

    f32 delta = deltaSeconds;
    if (!std::isfinite(delta) || delta < 0.0f)
    {
        delta = 0.0f;
    }
    if (delta > kMaxFlyDeltaSeconds)
    {
        delta = kMaxFlyDeltaSeconds;
    }

    f32 forward = input.moveForward;
    f32 right = input.moveRight;
    f32 up = input.moveUp;
    if (!std::isfinite(forward) || !std::isfinite(right) || !std::isfinite(up))
    {
        return;
    }

    const f32 lengthSquared = forward * forward + right * right + up * up;
    if (lengthSquared > 1.0f)
    {
        const f32 inverseLength = 1.0f / std::sqrt(lengthSquared);
        forward *= inverseLength;
        right *= inverseLength;
        up *= inverseLength;
    }

    f32 speed = fly.moveSpeed;
    if (input.fast)
    {
        speed *= fly.fastMultiplier;
    }
    if (!std::isfinite(speed) || speed <= 0.0f)
    {
        return;
    }

    const f32 distance = speed * delta;
    camera.position += CameraForward(camera) * (forward * distance) +
                       CameraRight(camera) * (right * distance) +
                       CameraUp(camera) * (up * distance);
}

cyber::math::Vec3 OrbitCameraPosition(const OrbitCamera& camera) noexcept
{
    const f32 cosPitch = std::cos(camera.pitchRadians);
    const cyber::math::Vec3 offset{std::sin(camera.yawRadians) * cosPitch,
                                   std::sin(camera.pitchRadians),
                                   std::cos(camera.yawRadians) * cosPitch};
    return camera.target + offset * camera.distance;
}

cyber::math::Mat4 OrbitCameraView(const OrbitCamera& camera) noexcept
{
    return cyber::math::Mat4::LookAt(OrbitCameraPosition(camera), camera.target, kWorldUp);
}

cyber::math::Mat4 OrbitCameraProjection(const OrbitCamera& camera, f32 aspect) noexcept
{
    if (!std::isfinite(aspect) || aspect <= 0.0f || !std::isfinite(camera.fovYRadians) ||
        camera.fovYRadians <= 0.0f || camera.fovYRadians >= cyber::math::kPi ||
        !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) ||
        camera.nearPlane <= 0.0f || camera.farPlane <= camera.nearPlane)
    {
        return cyber::math::Mat4::Identity();
    }
    return cyber::math::Mat4::PerspectiveReverseZ(camera.fovYRadians, aspect, camera.nearPlane,
                                                  camera.farPlane);
}

void UpdateOrbitCamera(OrbitCamera& camera, const OrbitCameraInput& input,
                       f32 deltaSeconds) noexcept
{
    (void)deltaSeconds;

    camera.yawRadians += input.yawDelta;
    camera.pitchRadians =
        cyber::math::Clamp(camera.pitchRadians + input.pitchDelta, -kMaxPitch, kMaxPitch);

    if (std::isfinite(input.zoomDelta))
    {
        camera.distance *= std::exp(-input.zoomDelta);
    }
    if (!std::isfinite(camera.distance))
    {
        camera.distance = camera.maxDistance;
    }

    f32 minDistance = camera.minDistance;
    if (!std::isfinite(minDistance) || minDistance < 0.0f)
    {
        minDistance = 0.0f;
    }
    f32 maxDistance = camera.maxDistance;
    if (!std::isfinite(maxDistance) || maxDistance < minDistance)
    {
        maxDistance = minDistance;
    }
    camera.distance = cyber::math::Clamp(camera.distance, minDistance, maxDistance);

    if (std::isfinite(input.panRight) && input.panRight != 0.0f)
    {
        camera.target += HorizontalRight(camera.yawRadians) * input.panRight;
    }
    if (std::isfinite(input.panUp) && input.panUp != 0.0f)
    {
        camera.target += kWorldUp * input.panUp;
    }
}

}  // namespace cyber::software

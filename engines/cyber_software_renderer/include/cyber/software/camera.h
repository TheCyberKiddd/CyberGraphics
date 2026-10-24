#pragma once

#include <cyber/core/types.h>
#include <cyber/math/constants.h>
#include <cyber/math/mat4.h>
#include <cyber/math/types.h>
#include <cyber/math/vec3.h>

namespace cyber::software
{

using cyber::core::f32;

// A right-handed perspective camera.
//
// Yaw rotates around the world +Y axis; yaw 0 looks toward -Z and positive yaw
// turns toward +X. Pitch rotates around the camera's local X axis; positive
// pitch looks up. The controllers below clamp pitch short of the poles, so the
// view basis never becomes degenerate.
//
// `fovYRadians`, `nearPlane`, and `farPlane` are passed to
// `math::Mat4::PerspectiveReverseZ`, which maps the near plane to depth 1 and
// the far plane to depth 0. Callers must supply 0 < fovYRadians < pi and
// 0 < nearPlane < farPlane.
struct Camera
{
    math::Vec3 position{0.0f, 0.0f, 0.0f};
    f32 yawRadians = 0.0f;
    f32 pitchRadians = 0.0f;
    f32 fovYRadians = 60.0f * cyber::math::kDegreesToRadians;
    f32 nearPlane = 0.1f;
    f32 farPlane = 1000.0f;
};

// Unit vector the camera looks along. Finite for every input; a pitch beyond
// the poles mirrors the forward direction, so use the controllers or clamp
// before calling.
[[nodiscard]] math::Vec3 CameraForward(const Camera& camera) noexcept;

// Unit vector pointing to the camera's right.
[[nodiscard]] math::Vec3 CameraRight(const Camera& camera) noexcept;

// Unit vector pointing up in the camera's local frame.
[[nodiscard]] math::Vec3 CameraUp(const Camera& camera) noexcept;

// Right-handed view matrix (world to view), built with `math::Mat4::LookAt`.
[[nodiscard]] math::Mat4 CameraView(const Camera& camera) noexcept;

// Reverse-Z perspective projection with a y-up clip space. Returns identity
// when aspect is not finite and positive or the depth range is invalid.
[[nodiscard]] math::Mat4 CameraProjection(const Camera& camera, f32 aspect) noexcept;

// Delta input for a fly camera. Move axes are in [-1, 1] and are measured in
// the camera's local frame; yaw and pitch are radians added this update.
struct FlyCameraInput
{
    f32 moveForward = 0.0f;
    f32 moveRight = 0.0f;
    f32 moveUp = 0.0f;
    f32 yawDelta = 0.0f;
    f32 pitchDelta = 0.0f;
    // Moves at moveSpeed * fastMultiplier instead of moveSpeed.
    bool fast = false;
};

// Free-flying first-person camera.
struct FlyCamera
{
    Camera camera{};
    // Meters per second at a delta time of one second.
    f32 moveSpeed = 4.0f;
    f32 fastMultiplier = 4.0f;
};

// Applies yaw and pitch, clamps pitch short of the poles, then moves along the
// camera's local axes by `moveSpeed * deltaSeconds`. A diagonal move is
// normalized so it is not faster than an axis-aligned one. `deltaSeconds` is
// clamped to [0, 0.25] so a debugger stall cannot teleport the camera.
void UpdateFlyCamera(FlyCamera& fly, const FlyCameraInput& input, f32 deltaSeconds) noexcept;

// Orbit camera around a target. The eye sits at
// `target + (sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch)) * distance`,
// so yaw 0 and pitch 0 place it on the +Z side of the target looking toward -Z.
struct OrbitCamera
{
    math::Vec3 target{0.0f, 0.0f, 0.0f};
    f32 distance = 6.0f;
    f32 yawRadians = 0.0f;
    f32 pitchRadians = 0.35f;
    f32 minDistance = 0.5f;
    f32 maxDistance = 100.0f;
    f32 fovYRadians = 60.0f * cyber::math::kDegreesToRadians;
    f32 nearPlane = 0.1f;
    f32 farPlane = 1000.0f;
};

// Delta input for an orbit camera. Pan is in world units along the camera's
// right and up axes; a positive zoomDelta moves the eye closer.
struct OrbitCameraInput
{
    f32 yawDelta = 0.0f;
    f32 pitchDelta = 0.0f;
    f32 zoomDelta = 0.0f;
    f32 panRight = 0.0f;
    f32 panUp = 0.0f;
};

// Eye position for the current orbit state.
[[nodiscard]] math::Vec3 OrbitCameraPosition(const OrbitCamera& camera) noexcept;

// View matrix looking from the orbit eye at the target.
[[nodiscard]] math::Mat4 OrbitCameraView(const OrbitCamera& camera) noexcept;

// Reverse-Z projection for the orbit camera's field of view and depth range.
[[nodiscard]] math::Mat4 OrbitCameraProjection(const OrbitCamera& camera, f32 aspect) noexcept;

// Applies the delta input: adds yaw and pitch (clamped short of the poles),
// moves the target in the camera's right/up plane, and scales the distance by
// `exp(-zoomDelta)` before clamping it to [minDistance, maxDistance]. Distance
// never reaches zero. `deltaSeconds` is reserved for future smoothing and is
// currently unused; pan is expressed in meters and needs no time step.
void UpdateOrbitCamera(OrbitCamera& camera, const OrbitCameraInput& input,
                       f32 deltaSeconds) noexcept;

}  // namespace cyber::software

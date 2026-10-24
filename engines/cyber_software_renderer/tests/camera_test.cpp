#include <cyber/math/constants.h>
#include <cyber/software/camera.h>
#include <cyber/test/test.h>

#include <cmath>

using namespace cyber::software;
using cyber::core::f32;
using cyber::math::ApproxEqual;
using cyber::math::kDegreesToRadians;
using cyber::math::kHalfPi;
using cyber::math::kPi;
using cyber::math::Mat4;
using cyber::math::Vec3;
using cyber::math::Vec4;

namespace
{

bool Near(f32 lhs, f32 rhs, f32 tolerance = 1.0e-5f)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

bool IsUnit(const Vec3& value)
{
    return Near(cyber::math::LengthSquared(value), 1.0f, 1.0e-5f);
}

}  // namespace

CG_TEST(software_camera_basis_and_view)
{
    Camera camera;
    camera.position = Vec3{3.0f, 4.0f, 5.0f};

    CG_EXPECT_TRUE(ApproxEqual(CameraForward(camera), Vec3{0.0f, 0.0f, -1.0f}));
    CG_EXPECT_TRUE(ApproxEqual(CameraRight(camera), Vec3{1.0f, 0.0f, 0.0f}));
    CG_EXPECT_TRUE(ApproxEqual(CameraUp(camera), Vec3{0.0f, 1.0f, 0.0f}));

    camera.yawRadians = kHalfPi;
    CG_EXPECT_TRUE(ApproxEqual(CameraForward(camera), Vec3{1.0f, 0.0f, 0.0f}));
    CG_EXPECT_TRUE(ApproxEqual(CameraRight(camera), Vec3{0.0f, 0.0f, 1.0f}));
    CG_EXPECT_TRUE(ApproxEqual(CameraUp(camera), Vec3{0.0f, 1.0f, 0.0f}));

    camera.yawRadians = 0.0f;
    camera.pitchRadians = kPi * 0.25f;
    const Vec3 forward = CameraForward(camera);
    CG_EXPECT_TRUE(IsUnit(forward));
    CG_EXPECT_TRUE(IsUnit(CameraRight(camera)));
    CG_EXPECT_TRUE(IsUnit(CameraUp(camera)));
    CG_EXPECT_TRUE(Near(forward.y, std::sin(kPi * 0.25f)));

    const Mat4 view = CameraView(camera);
    const Vec3 eyeInView = view.TransformPoint(camera.position);
    CG_EXPECT_TRUE(ApproxEqual(eyeInView, Vec3{0.0f, 0.0f, 0.0f}, 1.0e-5f));

    const Vec3 ahead = camera.position + forward * 10.0f;
    const Vec3 aheadInView = view.TransformPoint(ahead);
    CG_EXPECT_TRUE(Near(aheadInView.x, 0.0f, 1.0e-4f));
    CG_EXPECT_TRUE(Near(aheadInView.y, 0.0f, 1.0e-4f));
    CG_EXPECT_TRUE(Near(aheadInView.z, -10.0f, 1.0e-4f));
}

CG_TEST(software_camera_projection_reverse_z)
{
    Camera camera;
    camera.nearPlane = 0.5f;
    camera.farPlane = 500.0f;
    camera.fovYRadians = 90.0f * kDegreesToRadians;

    const Mat4 projection = CameraProjection(camera, 16.0f / 9.0f);
    const Vec3 nearPoint = projection.TransformPoint(Vec3{0.0f, 0.0f, -0.5f});
    const Vec3 farPoint = projection.TransformPoint(Vec3{0.0f, 0.0f, -500.0f});
    CG_EXPECT_TRUE(Near(nearPoint.z, 1.0f));
    CG_EXPECT_TRUE(Near(farPoint.z, 0.0f));

    // A point 1 unit right of the axis projects to x = 1 / aspect at z = -1
    // with a 90-degree vertical field of view.
    const f32 aspect = 2.0f;
    const Vec4 clip = CameraProjection(camera, aspect) * Vec4{1.0f, 0.0f, -1.0f, 1.0f};
    CG_EXPECT_TRUE(Near(clip.x / clip.w, 0.5f));
    CG_EXPECT_TRUE(Near(clip.y / clip.w, 0.0f));

    Camera invalid = camera;
    invalid.nearPlane = 0.0f;
    CG_EXPECT_TRUE(ApproxEqual(CameraProjection(invalid, 1.0f), Mat4::Identity()));
    CG_EXPECT_TRUE(ApproxEqual(CameraProjection(camera, 0.0f), Mat4::Identity()));
    CG_EXPECT_TRUE(ApproxEqual(CameraProjection(camera, std::nanf("")), Mat4::Identity()));

    invalid = camera;
    invalid.fovYRadians = kPi;
    CG_EXPECT_TRUE(ApproxEqual(CameraProjection(invalid, 1.0f), Mat4::Identity()));
}

CG_TEST(software_camera_fly_movement)
{
    FlyCamera fly;
    fly.moveSpeed = 4.0f;

    FlyCameraInput input;
    input.moveForward = 1.0f;
    UpdateFlyCamera(fly, input, 0.25f);
    CG_EXPECT_TRUE(ApproxEqual(fly.camera.position, Vec3{0.0f, 0.0f, -1.0f}));

    // Movement is frame-rate independent.
    FlyCamera split;
    split.moveSpeed = 4.0f;
    UpdateFlyCamera(split, input, 0.0625f);
    UpdateFlyCamera(split, input, 0.0625f);
    UpdateFlyCamera(split, input, 0.0625f);
    UpdateFlyCamera(split, input, 0.0625f);
    CG_EXPECT_TRUE(ApproxEqual(split.camera.position, fly.camera.position, 1.0e-5f));

    // Yaw rotates the movement direction, not the step length.
    fly.camera.position = Vec3{0.0f, 0.0f, 0.0f};
    input = FlyCameraInput{};
    input.yawDelta = kHalfPi;
    input.moveForward = 1.0f;
    UpdateFlyCamera(fly, input, 0.25f);
    CG_EXPECT_TRUE(ApproxEqual(fly.camera.position, Vec3{1.0f, 0.0f, 0.0f}, 1.0e-5f));

    // A diagonal move is normalized.
    fly.camera.position = Vec3{0.0f, 0.0f, 0.0f};
    fly.camera.yawRadians = 0.0f;
    input = FlyCameraInput{};
    input.moveForward = 1.0f;
    input.moveRight = 1.0f;
    UpdateFlyCamera(fly, input, 0.25f);
    const f32 diagonal = cyber::math::Length(fly.camera.position);
    CG_EXPECT_TRUE(Near(diagonal, 1.0f, 1.0e-4f));

    // Fast movement multiplies the speed.
    fly.camera.position = Vec3{0.0f, 0.0f, 0.0f};
    fly.camera.yawRadians = 0.0f;
    input = FlyCameraInput{};
    input.moveForward = 1.0f;
    input.fast = true;
    UpdateFlyCamera(fly, input, 0.25f);
    CG_EXPECT_TRUE(ApproxEqual(fly.camera.position, Vec3{0.0f, 0.0f, -4.0f}));

    // A stalled frame cannot teleport the camera.
    fly.camera.position = Vec3{0.0f, 0.0f, 0.0f};
    input = FlyCameraInput{};
    input.moveForward = 1.0f;
    UpdateFlyCamera(fly, input, 1000.0f);
    CG_EXPECT_TRUE(ApproxEqual(fly.camera.position, Vec3{0.0f, 0.0f, -1.0f}));

    // A negative or non-finite delta leaves the position alone.
    fly.camera.position = Vec3{1.0f, 2.0f, 3.0f};
    UpdateFlyCamera(fly, input, -1.0f);
    UpdateFlyCamera(fly, input, std::nanf(""));
    CG_EXPECT_TRUE(ApproxEqual(fly.camera.position, Vec3{1.0f, 2.0f, 3.0f}));
}

CG_TEST(software_camera_pitch_clamps)
{
    FlyCamera fly;
    FlyCameraInput input;
    input.pitchDelta = 10.0f;
    UpdateFlyCamera(fly, input, 0.016f);
    CG_EXPECT_TRUE(Near(fly.camera.pitchRadians, kHalfPi - 1.0e-3f));

    input.pitchDelta = -20.0f;
    UpdateFlyCamera(fly, input, 0.016f);
    CG_EXPECT_TRUE(Near(fly.camera.pitchRadians, -(kHalfPi - 1.0e-3f)));

    OrbitCamera orbit;
    OrbitCameraInput orbitInput;
    orbitInput.pitchDelta = 10.0f;
    UpdateOrbitCamera(orbit, orbitInput, 0.016f);
    CG_EXPECT_TRUE(Near(orbit.pitchRadians, kHalfPi - 1.0e-3f));
    CG_EXPECT_TRUE(std::isfinite(OrbitCameraPosition(orbit).x));

    orbitInput.pitchDelta = -20.0f;
    UpdateOrbitCamera(orbit, orbitInput, 0.016f);
    CG_EXPECT_TRUE(Near(orbit.pitchRadians, -(kHalfPi - 1.0e-3f)));
}

CG_TEST(software_camera_orbit_state)
{
    OrbitCamera orbit;
    orbit.target = Vec3{1.0f, 2.0f, 3.0f};
    orbit.distance = 5.0f;
    orbit.yawRadians = 0.0f;
    orbit.pitchRadians = 0.0f;

    CG_EXPECT_TRUE(ApproxEqual(OrbitCameraPosition(orbit), Vec3{1.0f, 2.0f, 8.0f}));

    orbit.yawRadians = kHalfPi;
    CG_EXPECT_TRUE(ApproxEqual(OrbitCameraPosition(orbit), Vec3{6.0f, 2.0f, 3.0f}, 1.0e-5f));

    orbit.yawRadians = 0.0f;
    orbit.pitchRadians = kHalfPi * 0.5f;
    const Vec3 high = OrbitCameraPosition(orbit);
    CG_EXPECT_TRUE(Near(high.y, 2.0f + 5.0f * std::sin(kPi * 0.25f), 1.0e-4f));

    const Mat4 view = OrbitCameraView(orbit);
    const Vec3 eye = OrbitCameraPosition(orbit);
    CG_EXPECT_TRUE(ApproxEqual(view.TransformPoint(eye), Vec3{0.0f, 0.0f, 0.0f}, 1.0e-5f));
    const Vec3 targetInView = view.TransformPoint(orbit.target);
    CG_EXPECT_TRUE(Near(targetInView.x, 0.0f, 1.0e-4f));
    CG_EXPECT_TRUE(Near(targetInView.y, 0.0f, 1.0e-4f));
    CG_EXPECT_TRUE(targetInView.z < 0.0f);

    // Zoom is exponential and clamps without crossing zero.
    OrbitCameraInput input;
    input.zoomDelta = 100.0f;
    UpdateOrbitCamera(orbit, input, 0.016f);
    CG_EXPECT_EQ(orbit.distance, orbit.minDistance);

    input.zoomDelta = -100.0f;
    UpdateOrbitCamera(orbit, input, 0.016f);
    CG_EXPECT_EQ(orbit.distance, orbit.maxDistance);

    // Panning moves the target in world space.
    orbit.distance = 5.0f;
    orbit.yawRadians = 0.0f;
    orbit.target = Vec3{0.0f, 0.0f, 0.0f};
    input = OrbitCameraInput{};
    input.panRight = 2.0f;
    input.panUp = 1.0f;
    UpdateOrbitCamera(orbit, input, 0.016f);
    CG_EXPECT_TRUE(ApproxEqual(orbit.target, Vec3{2.0f, 1.0f, 0.0f}));

    // A broken zoom input cannot produce a non-finite distance.
    input = OrbitCameraInput{};
    input.zoomDelta = std::nanf("");
    UpdateOrbitCamera(orbit, input, 0.016f);
    CG_EXPECT_TRUE(std::isfinite(orbit.distance));
    CG_EXPECT_TRUE(orbit.distance >= orbit.minDistance);

    CG_EXPECT_TRUE(std::isfinite(OrbitCameraProjection(orbit, 1.0f).columns[0].x));
    CG_EXPECT_TRUE(ApproxEqual(OrbitCameraProjection(orbit, -1.0f), Mat4::Identity()));
}

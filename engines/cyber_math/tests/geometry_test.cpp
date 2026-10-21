#include <cyber/math/math.h>
#include <cyber/test/test.h>

using namespace cyber::math;

CG_TEST(math_aabb_empty_and_expand)
{
    const Aabb empty = Aabb::Empty();
    CG_EXPECT_TRUE(empty.IsEmpty());
    CG_EXPECT_TRUE(ApproxEqual(empty.Center(), Vec3::Zero()));
    CG_EXPECT_FALSE(empty.Contains(Vec3::Zero()));
    CG_EXPECT_FALSE(empty.Overlaps(Aabb::FromMinMax(Vec3::Zero(), Vec3::One())));

    Aabb box = empty;
    box.Expand(Vec3{1.0f, 2.0f, 3.0f});
    CG_EXPECT_FALSE(box.IsEmpty());
    CG_EXPECT_TRUE(ApproxEqual(box.Min(), Vec3{1.0f, 2.0f, 3.0f}));
    CG_EXPECT_TRUE(ApproxEqual(box.Max(), Vec3{1.0f, 2.0f, 3.0f}));

    box.Expand(Vec3{-1.0f, 4.0f, 0.0f});
    CG_EXPECT_TRUE(ApproxEqual(box.Min(), Vec3{-1.0f, 2.0f, 0.0f}));
    CG_EXPECT_TRUE(ApproxEqual(box.Max(), Vec3{1.0f, 4.0f, 3.0f}));
    CG_EXPECT_TRUE(ApproxEqual(box.Center(), Vec3{0.0f, 3.0f, 1.5f}));
    CG_EXPECT_TRUE(ApproxEqual(box.Extents(), Vec3{1.0f, 1.0f, 1.5f}));
}

CG_TEST(math_aabb_contains_and_overlaps)
{
    const Aabb box = Aabb::FromMinMax(Vec3{-1.0f, -2.0f, -3.0f}, Vec3{1.0f, 2.0f, 3.0f});
    CG_EXPECT_TRUE(box.Contains(Vec3::Zero()));
    CG_EXPECT_TRUE(box.Contains(Vec3{-1.0f, 2.0f, -3.0f}));
    CG_EXPECT_FALSE(box.Contains(Vec3{1.1f, 0.0f, 0.0f}));

    const Aabb overlapping = Aabb::FromMinMax(Vec3{0.5f, 0.5f, 0.5f}, Vec3{2.0f, 2.0f, 2.0f});
    const Aabb disjoint = Aabb::FromMinMax(Vec3{3.0f, 3.0f, 3.0f}, Vec3{4.0f, 4.0f, 4.0f});
    CG_EXPECT_TRUE(box.Overlaps(overlapping));
    CG_EXPECT_TRUE(overlapping.Overlaps(box));
    CG_EXPECT_FALSE(box.Overlaps(disjoint));

    const Aabb fromCenter = Aabb::FromCenterExtents(Vec3::Zero(), Vec3{1.0f, 2.0f, 3.0f});
    CG_EXPECT_TRUE(ApproxEqual(fromCenter, box));

    const Aabb invalid = Aabb::FromMinMax(Vec3{1.0f, 0.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f});
    CG_EXPECT_TRUE(invalid.IsEmpty());
}

CG_TEST(math_plane_distance_and_normalization)
{
    const Plane plane = FromPointNormal(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 2.0f, 0.0f});
    CG_EXPECT_NEAR(DistanceTo(plane, Vec3{0.0f, 3.0f, 0.0f}), 3.0f, 1.0e-5f);
    CG_EXPECT_NEAR(DistanceTo(plane, Vec3{0.0f, -1.0f, 0.0f}), -1.0f, 1.0e-5f);

    const Plane unnormalized{Vec3{0.0f, 2.0f, 0.0f}, 4.0f};
    const Plane normalized = Normalize(unnormalized);
    CG_EXPECT_TRUE(ApproxEqual(normalized.normal, Vec3{0.0f, 1.0f, 0.0f}));
    CG_EXPECT_NEAR(normalized.distance, 2.0f, 1.0e-5f);
    CG_EXPECT_NEAR(DistanceTo(normalized, Vec3{0.0f, 1.0f, 0.0f}), 3.0f, 1.0e-5f);
}

CG_TEST(math_ray_aabb)
{
    const Aabb box = Aabb::FromMinMax(Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f});

    f32 distance = -1.0f;
    const Ray fromFront{Vec3{0.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, -1.0f}};
    CG_EXPECT_TRUE(Intersect(fromFront, box, &distance));
    CG_EXPECT_NEAR(distance, 4.0f, 1.0e-5f);
    CG_EXPECT_FALSE(Intersect(Ray{Vec3{0.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, 1.0f}}, box));
    CG_EXPECT_FALSE(Intersect(Ray{Vec3{2.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, -1.0f}}, box));

    const Ray inside{Vec3::Zero(), Vec3{0.0f, 0.0f, -1.0f}};
    CG_EXPECT_TRUE(Intersect(inside, box, &distance));
    CG_EXPECT_NEAR(distance, 0.0f, 1.0e-5f);

    const f32 untouched = 42.0f;
    distance = untouched;
    CG_EXPECT_FALSE(
        Intersect(Ray{Vec3{2.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, -1.0f}}, box, &distance));
    CG_EXPECT_NEAR(distance, untouched, 0.0f);
}

CG_TEST(math_ray_sphere)
{
    const Sphere sphere{Vec3::Zero(), 1.0f};

    f32 distance = -1.0f;
    CG_EXPECT_TRUE(
        Intersect(Ray{Vec3{0.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, -1.0f}}, sphere, &distance));
    CG_EXPECT_NEAR(distance, 4.0f, 1.0e-5f);
    CG_EXPECT_FALSE(Intersect(Ray{Vec3{2.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, -1.0f}}, sphere));

    CG_EXPECT_TRUE(Intersect(Ray{Vec3::Zero(), Vec3{1.0f, 0.0f, 0.0f}}, sphere, &distance));
    CG_EXPECT_NEAR(distance, 1.0f, 1.0e-5f);
    CG_EXPECT_TRUE(sphere.Contains(Vec3{0.5f, 0.5f, 0.5f}));
    CG_EXPECT_FALSE(sphere.Contains(Vec3{1.0f, 1.0f, 1.0f}));
}

CG_TEST(math_frustum_extraction_and_culling)
{
    const Mat4 projection = Mat4::PerspectiveReverseZ(kHalfPi, 1.0f, 0.1f, 10.0f);
    const Frustum frustum = Frustum::FromViewProjection(projection);

    CG_EXPECT_TRUE(Contains(frustum, Vec3{0.0f, 0.0f, -5.0f}));
    CG_EXPECT_TRUE(Contains(frustum, Vec3{0.0f, 0.0f, -0.2f}));
    CG_EXPECT_FALSE(Contains(frustum, Vec3{0.0f, 0.0f, -0.05f}));
    CG_EXPECT_FALSE(Contains(frustum, Vec3{0.0f, 0.0f, -11.0f}));
    CG_EXPECT_FALSE(Contains(frustum, Vec3{0.0f, 0.0f, 1.0f}));
    CG_EXPECT_FALSE(Contains(frustum, Vec3{100.0f, 0.0f, -5.0f}));

    const Aabb inside = Aabb::FromMinMax(Vec3{-1.0f, -1.0f, -6.0f}, Vec3{1.0f, 1.0f, -4.0f});
    const Aabb outside =
        Aabb::FromMinMax(Vec3{100.0f, 100.0f, 100.0f}, Vec3{101.0f, 101.0f, 101.0f});
    const Aabb behind = Aabb::FromMinMax(Vec3{-1.0f, -1.0f, 1.0f}, Vec3{1.0f, 1.0f, 2.0f});
    CG_EXPECT_TRUE(Intersects(frustum, inside));
    CG_EXPECT_FALSE(Intersects(frustum, outside));
    CG_EXPECT_FALSE(Intersects(frustum, behind));
    CG_EXPECT_FALSE(Intersects(frustum, Aabb::Empty()));

    const Mat4 view = Mat4::LookAt(Vec3{0.0f, 0.0f, 5.0f}, Vec3::Zero(), Vec3{0.0f, 1.0f, 0.0f});
    const Frustum viewFrustum = Frustum::FromViewProjection(projection * view);
    CG_EXPECT_TRUE(Contains(viewFrustum, Vec3::Zero()));
    CG_EXPECT_TRUE(Contains(viewFrustum, Vec3{0.0f, 0.0f, -4.0f}));
    CG_EXPECT_FALSE(Contains(viewFrustum, Vec3{0.0f, 0.0f, 6.0f}));
    CG_EXPECT_TRUE(Intersects(viewFrustum, inside));
    CG_EXPECT_TRUE(Intersects(viewFrustum, behind));

    const Aabb behindCamera = Aabb::FromMinMax(Vec3{-1.0f, -1.0f, 6.0f}, Vec3{1.0f, 1.0f, 7.0f});
    CG_EXPECT_FALSE(Intersects(viewFrustum, behindCamera));
}

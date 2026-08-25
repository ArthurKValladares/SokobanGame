#include "engine/Geometry.hpp"

#include <array>
#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void checkNear(float actual, float expected, const char* label, float tolerance = 1e-5f)
{
    if (!(std::abs(actual - expected) <= tolerance)) {
        ++failures;
        std::cerr << "FAIL: " << label << " (expected " << expected
                  << ", got " << actual << ")\n";
    }
}

using namespace sokoban;

void checkNear(Vec3 actual, Vec3 expected, const char* label, float tolerance = 1e-5f)
{
    checkNear(actual.x, expected.x, label, tolerance);
    checkNear(actual.y, expected.y, label, tolerance);
    checkNear(actual.z, expected.z, label, tolerance);
}

void testAabbBuilding()
{
    // The inverted default is what makes accumulation work without a special
    // case for the first point.
    const Aabb empty;
    check(!empty.valid(), "a default Aabb is invalid, not a box at the origin");
    const Aabb single = expand(empty, { 1.0f, 2.0f, 3.0f });
    check(single.valid(), "one point makes a valid box");
    checkNear(single.minimum, { 1.0f, 2.0f, 3.0f }, "a one-point box has that minimum");
    checkNear(single.maximum, { 1.0f, 2.0f, 3.0f }, "a one-point box has that maximum");
    checkNear(extents(single), {}, "a one-point box has zero extents");

    const std::array<Vec3, 4> points {
        Vec3 { -1.0f, 0.0f, 2.0f },
        Vec3 { 3.0f, -4.0f, 2.0f },
        Vec3 { 0.0f, 1.0f, -5.0f },
        Vec3 { 1.0f, 1.0f, 1.0f },
    };
    const Aabb box = aabbFromPoints(points);
    checkNear(box.minimum, { -1.0f, -4.0f, -5.0f }, "bounds take the componentwise minimum");
    checkNear(box.maximum, { 3.0f, 1.0f, 2.0f }, "bounds take the componentwise maximum");
    checkNear(center(box), { 1.0f, -1.5f, -1.5f }, "centre is the midpoint");
    checkNear(extents(box), { 2.0f, 2.5f, 3.5f }, "extents are half the size");

    // A flat box is legitimate: a ground quad has no thickness.
    const Aabb flat = aabbFromMinMax({ 0.0f, 0.0f, 5.0f }, { 2.0f, 2.0f, 5.0f });
    check(flat.valid(), "a zero-thickness box is still valid");

    // Argument order must not matter.
    check(aabbFromMinMax({ 3.0f, 3.0f, 3.0f }, { 1.0f, 1.0f, 1.0f }) ==
            aabbFromMinMax({ 1.0f, 1.0f, 1.0f }, { 3.0f, 3.0f, 3.0f }),
        "aabbFromMinMax sorts its corners");

    check(merge(empty, box) == box, "merging with an invalid box is a no-op");
    check(merge(box, empty) == box, "merging an invalid box in is a no-op");
}

void testAabbQueries()
{
    const Aabb box = aabbFromMinMax({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f });
    check(contains(box, { 1.0f, 1.0f, 1.0f }), "an interior point is contained");
    check(contains(box, { 0.0f, 0.0f, 0.0f }), "a corner counts as contained");
    check(contains(box, { 2.0f, 2.0f, 2.0f }), "the far corner counts as contained");
    check(!contains(box, { 2.1f, 1.0f, 1.0f }), "a point past a face is not contained");

    check(intersects(box, aabbFromMinMax({ 1.0f, 1.0f, 1.0f }, { 3.0f, 3.0f, 3.0f })),
        "overlapping boxes intersect");
    check(intersects(box, aabbFromMinMax({ 2.0f, 0.0f, 0.0f }, { 4.0f, 2.0f, 2.0f })),
        "touching boxes intersect");
    check(!intersects(box, aabbFromMinMax({ 2.1f, 0.0f, 0.0f }, { 4.0f, 2.0f, 2.0f })),
        "separated boxes do not intersect");
    check(!intersects(box, Aabb {}), "nothing intersects an invalid box");
}

void testAabbTransform()
{
    const Aabb unit = aabbFromMinMax({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });

    const Aabb moved = transformed(mat4FromTranslation({ 5.0f, 0.0f, 0.0f }), unit);
    checkNear(center(moved), { 5.0f, 0.0f, 0.0f }, "translation moves the centre");
    checkNear(extents(moved), { 1.0f, 1.0f, 1.0f }, "translation does not change extents");

    const Aabb scaled = transformed(mat4FromScale({ 2.0f, 3.0f, 4.0f }), unit);
    checkNear(extents(scaled), { 2.0f, 3.0f, 4.0f }, "scale multiplies extents");

    // A 45 degree turn about z must grow the box to sqrt(2) on x and y, and
    // leave z alone. This is the case where transforming the bounds and
    // bounding the transform genuinely differ.
    const Mat4 spin = mat4FromQuat(quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, pi * 0.25f));
    const Aabb spun = transformed(spin, unit);
    checkNear(extents(spun).x, std::sqrt(2.0f), "a rotated box grows on x", 1e-4f);
    checkNear(extents(spun).y, std::sqrt(2.0f), "a rotated box grows on y", 1e-4f);
    checkNear(extents(spun).z, 1.0f, "a rotation about z leaves z alone", 1e-4f);

    // A quarter turn maps the box exactly onto itself.
    const Mat4 quarter = mat4FromQuat(quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, pi * 0.5f));
    const Aabb square = transformed(quarter, unit);
    checkNear(extents(square), { 1.0f, 1.0f, 1.0f },
        "a quarter turn maps a cube onto itself", 1e-4f);

    check(!transformed(mat4Identity, Aabb {}).valid(),
        "transforming an invalid box leaves it invalid");
}

void testPlaneAndRay()
{
    const Plane ground = planeFromPointNormal({ 0.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 2.0f });
    checkNear(length(ground.normal), 1.0f, "planeFromPointNormal normalizes");
    checkNear(signedDistance(ground, { 9.0f, -4.0f, 3.0f }), 0.0f, "a point on the plane is at zero");
    checkNear(signedDistance(ground, { 0.0f, 0.0f, 5.0f }), 2.0f, "above the plane is positive");
    checkNear(signedDistance(ground, { 0.0f, 0.0f, 1.0f }), -2.0f, "below the plane is negative");

    const Ray down { { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f } };
    const std::optional<float> hit = intersect(down, ground);
    check(hit.has_value(), "a ray aimed at the plane hits it");
    if (hit) {
        checkNear(*hit, 7.0f, "the hit distance is along the direction vector");
        checkNear(pointAt(down, *hit), { 0.0f, 0.0f, 3.0f }, "the hit point is on the plane");
    }
    check(!intersect(Ray { { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, 1.0f } }, ground).has_value(),
        "a ray pointing away misses");
    check(!intersect(Ray { { 0.0f, 0.0f, 10.0f }, { 1.0f, 0.0f, 0.0f } }, ground).has_value(),
        "a ray parallel to the plane misses");

    const Aabb box = aabbFromMinMax({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });
    const std::optional<float> boxHit =
        intersect(Ray { { -5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, box);
    check(boxHit.has_value(), "a ray aimed at the box hits it");
    if (boxHit) {
        checkNear(*boxHit, 4.0f, "the box hit distance is to the near face");
    }
    const std::optional<float> inside =
        intersect(Ray { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, box);
    check(inside.has_value(), "a ray starting inside hits");
    if (inside) {
        checkNear(*inside, 0.0f, "a ray starting inside reports zero distance");
    }
    check(!intersect(Ray { { -5.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, box).has_value(),
        "a ray passing beside the box misses");
    check(!intersect(Ray { { 5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, box).has_value(),
        "a ray pointing away from the box misses");
    // Parallel to two slabs and outside one of them.
    check(!intersect(Ray { { 0.0f, 9.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, box).has_value(),
        "a ray parallel to a slab it is outside of misses");
}

// A Vulkan-convention perspective with +z forward in camera space, matching
// what projectIsoPointToClip produces: depth maps to 0..1 rather than -1..1.
// Built here rather than in the module because choosing a camera convention
// belongs with the camera, not with the geometry primitives.
[[nodiscard]] Mat4 perspectiveForTest(float focal, float aspect, float near, float far)
{
    Mat4 m {};
    m.values.fill(0.0f);
    const auto set = [&m](int row, int column, float value) {
        m.values[static_cast<std::size_t>(column) * 4 + static_cast<std::size_t>(row)] = value;
    };
    set(0, 0, focal / aspect);
    set(1, 1, focal);
    set(2, 2, far / (far - near));
    set(2, 3, -far * near / (far - near));
    set(3, 2, 1.0f);
    return m;
}

void testFrustum()
{
    constexpr float near = 1.0f;
    constexpr float far = 100.0f;
    const Mat4 projection = perspectiveForTest(1.0f, 1.0f, near, far);

    // Sanity-check the test's own matrix before trusting what it proves.
    const Vec4 atNear = transform(projection, { 0.0f, 0.0f, near, 1.0f });
    checkNear(atNear.z / atNear.w, 0.0f, "the test projection puts near at depth 0");
    const Vec4 atFar = transform(projection, { 0.0f, 0.0f, far, 1.0f });
    checkNear(atFar.z / atFar.w, 1.0f, "the test projection puts far at depth 1", 1e-4f);

    const Frustum frustum = frustumFromViewProjection(projection);
    for (const Plane& plane : frustum.planes) {
        checkNear(length(plane.normal), 1.0f, "every extracted plane is normalized", 1e-4f);
    }

    check(contains(frustum, { 0.0f, 0.0f, 50.0f }), "a point down the middle is inside");
    check(!contains(frustum, { 0.0f, 0.0f, 0.5f }), "a point nearer than near is outside");
    check(!contains(frustum, { 0.0f, 0.0f, 120.0f }), "a point beyond far is outside");
    check(!contains(frustum, { 0.0f, 0.0f, -10.0f }), "a point behind the camera is outside");
    check(!contains(frustum, { 60.0f, 0.0f, 50.0f }), "a point off to the side is outside");
    // With focal 1 and aspect 1 the half-angle is 45 degrees, so x == z is the
    // edge; comfortably inside and comfortably outside must classify apart.
    check(contains(frustum, { 40.0f, 0.0f, 50.0f }), "inside the cone is inside");
    check(!contains(frustum, { 55.0f, 0.0f, 50.0f }), "outside the cone is outside");

    check(intersects(frustum, aabbFromMinMax({ -1.0f, -1.0f, 40.0f }, { 1.0f, 1.0f, 60.0f })),
        "a box in view intersects");
    check(!intersects(frustum, aabbFromMinMax({ -1.0f, -1.0f, 200.0f }, { 1.0f, 1.0f, 260.0f })),
        "a box past the far plane does not intersect");
    check(!intersects(frustum, aabbFromMinMax({ 200.0f, -1.0f, 40.0f }, { 260.0f, 1.0f, 60.0f })),
        "a box off to the side does not intersect");
    // Straddling the near plane must count as visible: culling is allowed to
    // be conservative, never to drop something partly on screen.
    check(intersects(frustum, aabbFromMinMax({ -1.0f, -1.0f, -5.0f }, { 1.0f, 1.0f, 5.0f })),
        "a box straddling the near plane is kept");
    check(!intersects(frustum, Aabb {}), "an invalid box is never visible");

    check(intersects(frustum, Sphere { { 0.0f, 0.0f, 50.0f }, 1.0f }),
        "a sphere in view intersects");
    check(!intersects(frustum, Sphere { { 0.0f, 0.0f, 500.0f }, 1.0f }),
        "a distant sphere does not intersect");
    // A sphere whose centre is outside but whose surface reaches in.
    check(intersects(frustum, Sphere { { 0.0f, 0.0f, 0.0f }, 2.0f }),
        "a sphere reaching past the near plane is kept");

    const Sphere around = boundingSphere(
        aabbFromMinMax({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }));
    checkNear(around.radius, std::sqrt(3.0f), "a bounding sphere reaches the box corner", 1e-4f);
}

} // namespace

int main()
{
    testAabbBuilding();
    testAabbQueries();
    testAabbTransform();
    testPlaneAndRay();
    testFrustum();

    if (failures != 0) {
        std::cerr << "GeometryTests: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "GeometryTests passed\n";
    return 0;
}

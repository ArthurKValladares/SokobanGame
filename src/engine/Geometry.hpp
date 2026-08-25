#pragma once

#include "engine/Math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace sokoban {

// Bounding volumes, rays and frustums.
//
// Split from Math.hpp because far fewer translation units need these, and
// Math.hpp is included almost everywhere. Nothing here knows about rendering;
// these are the shapes culling, picking and streaming will address geometry
// with.

// --------------------------------------------------------------------- Aabb

// Axis-aligned bounds. A default-constructed Aabb is deliberately *inverted*
// (min above max) so that expanding it with the first point yields exactly
// that point, and `valid()` distinguishes "nothing here yet" from "a box at
// the origin" - a distinction a zero-initialized box cannot make.
struct Aabb {
    Vec3 minimum {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maximum {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };

    [[nodiscard]] constexpr bool valid() const
    {
        return minimum.x <= maximum.x && minimum.y <= maximum.y &&
            minimum.z <= maximum.z;
    }

    friend constexpr bool operator==(const Aabb&, const Aabb&) = default;
};

[[nodiscard]] constexpr Aabb aabbFromMinMax(Vec3 minimum, Vec3 maximum)
{
    return { minComponents(minimum, maximum), maxComponents(minimum, maximum) };
}

[[nodiscard]] constexpr Aabb expand(Aabb box, Vec3 point)
{
    return {
        minComponents(box.minimum, point),
        maxComponents(box.maximum, point),
    };
}

[[nodiscard]] constexpr Aabb merge(Aabb left, Aabb right)
{
    if (!left.valid()) {
        return right;
    }
    if (!right.valid()) {
        return left;
    }
    return {
        minComponents(left.minimum, right.minimum),
        maxComponents(left.maximum, right.maximum),
    };
}

[[nodiscard]] inline Aabb aabbFromPoints(std::span<const Vec3> points)
{
    Aabb result;
    for (Vec3 point : points) {
        result = expand(result, point);
    }
    return result;
}

[[nodiscard]] constexpr Vec3 center(Aabb box)
{
    return (box.minimum + box.maximum) * 0.5f;
}

// Half-size. Zero on an axis is legal: a flat quad has real bounds.
[[nodiscard]] constexpr Vec3 extents(Aabb box)
{
    return (box.maximum - box.minimum) * 0.5f;
}

[[nodiscard]] constexpr bool contains(Aabb box, Vec3 point)
{
    return point.x >= box.minimum.x && point.x <= box.maximum.x &&
        point.y >= box.minimum.y && point.y <= box.maximum.y &&
        point.z >= box.minimum.z && point.z <= box.maximum.z;
}

[[nodiscard]] constexpr bool intersects(Aabb left, Aabb right)
{
    return left.valid() && right.valid() &&
        left.minimum.x <= right.maximum.x && left.maximum.x >= right.minimum.x &&
        left.minimum.y <= right.maximum.y && left.maximum.y >= right.minimum.y &&
        left.minimum.z <= right.maximum.z && left.maximum.z >= right.minimum.z;
}

// The bounds of the transformed box, which is generally larger than the
// transform of the bounds: rotating a box and re-fitting an axis-aligned one
// around it cannot shrink it. Computed from the centre and the absolute
// linear part rather than by transforming eight corners.
[[nodiscard]] inline Aabb transformed(const Mat4& matrix, Aabb box)
{
    if (!box.valid()) {
        return box;
    }
    const Vec3 boxCenter = center(box);
    const Vec3 boxExtents = extents(box);
    const Vec3 newCenter = transformPoint(matrix, boxCenter);
    const Mat3 linear = linearPart(matrix);
    const Vec3 newExtents {
        std::abs(at(linear, 0, 0)) * boxExtents.x +
            std::abs(at(linear, 0, 1)) * boxExtents.y +
            std::abs(at(linear, 0, 2)) * boxExtents.z,
        std::abs(at(linear, 1, 0)) * boxExtents.x +
            std::abs(at(linear, 1, 1)) * boxExtents.y +
            std::abs(at(linear, 1, 2)) * boxExtents.z,
        std::abs(at(linear, 2, 0)) * boxExtents.x +
            std::abs(at(linear, 2, 1)) * boxExtents.y +
            std::abs(at(linear, 2, 2)) * boxExtents.z,
    };
    return { newCenter - newExtents, newCenter + newExtents };
}

// ------------------------------------------------------------------- Sphere

struct Sphere {
    Vec3 center {};
    float radius = 0.0f;

    friend constexpr bool operator==(const Sphere&, const Sphere&) = default;
};

[[nodiscard]] inline Sphere boundingSphere(Aabb box)
{
    return box.valid() ? Sphere { center(box), length(extents(box)) } : Sphere {};
}

// -------------------------------------------------------------------- Plane

// dot(normal, point) + distance. Positive is in front of the plane; for a
// frustum's planes, "in front" means inside.
struct Plane {
    Vec3 normal { 0.0f, 0.0f, 1.0f };
    float distance = 0.0f;

    friend constexpr bool operator==(const Plane&, const Plane&) = default;
};

[[nodiscard]] inline Plane planeFromPointNormal(Vec3 point, Vec3 normal)
{
    const Vec3 unit = normalizeOr(normal, Vec3 { 0.0f, 0.0f, 1.0f });
    return { unit, -dot(unit, point) };
}

[[nodiscard]] constexpr float signedDistance(Plane plane, Vec3 point)
{
    return dot(plane.normal, point) + plane.distance;
}

[[nodiscard]] inline Plane normalized(Plane plane)
{
    const float magnitude = length(plane.normal);
    if (magnitude <= 1e-6f) {
        return plane;
    }
    return { plane.normal / magnitude, plane.distance / magnitude };
}

// ---------------------------------------------------------------------- Ray

struct Ray {
    Vec3 origin {};
    // Not required to be unit length, but the distances every intersection
    // routine here returns are in units of this vector.
    Vec3 direction { 0.0f, 0.0f, 1.0f };
};

[[nodiscard]] constexpr Vec3 pointAt(const Ray& ray, float t)
{
    return ray.origin + ray.direction * t;
}

// Distance along the ray to the plane, or nothing when the ray is parallel to
// it or points away.
[[nodiscard]] inline std::optional<float> intersect(const Ray& ray, Plane plane)
{
    const float denominator = dot(plane.normal, ray.direction);
    if (std::abs(denominator) <= 1e-8f) {
        return std::nullopt;
    }
    const float t = -signedDistance(plane, ray.origin) / denominator;
    return t >= 0.0f ? std::optional<float> { t } : std::nullopt;
}

// Slab test. Returns the near hit distance, which is zero when the origin is
// already inside the box.
[[nodiscard]] inline std::optional<float> intersect(const Ray& ray, Aabb box)
{
    if (!box.valid()) {
        return std::nullopt;
    }
    float nearest = 0.0f;
    float farthest = std::numeric_limits<float>::max();
    const std::array<float, 3> origin { ray.origin.x, ray.origin.y, ray.origin.z };
    const std::array<float, 3> direction {
        ray.direction.x, ray.direction.y, ray.direction.z };
    const std::array<float, 3> minimum { box.minimum.x, box.minimum.y, box.minimum.z };
    const std::array<float, 3> maximum { box.maximum.x, box.maximum.y, box.maximum.z };

    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) <= 1e-8f) {
            // Parallel to this slab: a miss unless the origin is within it.
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
                return std::nullopt;
            }
            continue;
        }
        const float inverse = 1.0f / direction[axis];
        float entry = (minimum[axis] - origin[axis]) * inverse;
        float exit = (maximum[axis] - origin[axis]) * inverse;
        if (entry > exit) {
            std::swap(entry, exit);
        }
        nearest = std::max(nearest, entry);
        farthest = std::min(farthest, exit);
        if (nearest > farthest) {
            return std::nullopt;
        }
    }
    return nearest;
}

// ------------------------------------------------------------------ Frustum

// Six inward-facing planes. Naming follows clip space, not the screen: the
// scene pass draws through a negative-height viewport, so what clip space
// calls "top" appears at the bottom of the window. That does not affect any
// test here, which is why the planes are also addressable by index.
struct Frustum {
    enum Side : std::size_t {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5,
        SideCount = 6,
    };

    std::array<Plane, SideCount> planes {};
};

// Gribb-Hartmann plane extraction from a view-projection matrix.
//
// The near plane uses row 2 alone rather than row2 + row3 because Vulkan's
// clip volume is 0 <= z <= w, not -w <= z <= w. Using the OpenGL form here
// produces a near plane in the wrong place, and the symptom - geometry culled
// slightly too early near the camera - is easy to mistake for something else.
[[nodiscard]] inline Frustum frustumFromViewProjection(const Mat4& viewProjection)
{
    const auto row = [&viewProjection](int index) {
        return Vec4 {
            at(viewProjection, index, 0),
            at(viewProjection, index, 1),
            at(viewProjection, index, 2),
            at(viewProjection, index, 3),
        };
    };
    const Vec4 rowX = row(0);
    const Vec4 rowY = row(1);
    const Vec4 rowZ = row(2);
    const Vec4 rowW = row(3);

    const auto toPlane = [](Vec4 v) {
        return normalized(Plane { { v.x, v.y, v.z }, v.w });
    };

    Frustum result {};
    result.planes[Frustum::Left] = toPlane(rowW + rowX);
    result.planes[Frustum::Right] = toPlane(rowW - rowX);
    result.planes[Frustum::Bottom] = toPlane(rowW + rowY);
    result.planes[Frustum::Top] = toPlane(rowW - rowY);
    result.planes[Frustum::Near] = toPlane(rowZ);
    result.planes[Frustum::Far] = toPlane(rowW - rowZ);
    return result;
}

// Conservative: a box straddling a plane counts as visible, and a box outside
// two planes but inside all six half-spaces individually can produce a false
// positive. Both are the right way to be wrong for culling - the cost is a
// draw that contributes nothing, not geometry that vanishes.
[[nodiscard]] inline bool intersects(const Frustum& frustum, Aabb box)
{
    if (!box.valid()) {
        return false;
    }
    const Vec3 boxCenter = center(box);
    const Vec3 boxExtents = extents(box);
    for (const Plane& plane : frustum.planes) {
        // The box's radius projected onto the plane normal.
        const float radius = boxExtents.x * std::abs(plane.normal.x) +
            boxExtents.y * std::abs(plane.normal.y) +
            boxExtents.z * std::abs(plane.normal.z);
        if (signedDistance(plane, boxCenter) < -radius) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool intersects(const Frustum& frustum, Sphere sphere)
{
    for (const Plane& plane : frustum.planes) {
        if (signedDistance(plane, sphere.center) < -sphere.radius) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool contains(const Frustum& frustum, Vec3 point)
{
    for (const Plane& plane : frustum.planes) {
        if (signedDistance(plane, point) < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace sokoban

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace sokoban {

// The engine's linear algebra.
//
// Conventions, because getting one of these wrong is silent:
//
// - Matrices are COLUMN-MAJOR: `values[column * 4 + row]`. This matches GLSL
//   and Vulkan, so a Mat4 can be memcpy'd into a uniform or push constant
//   without transposing, and it matches the layout the glTF loader already
//   produced before this module existed.
// - Multiplication composes right to left: `a * b` applies b first. So a
//   model-view-projection is `projection * view * model`.
// - Quaternions are (x, y, z, w) with w last, matching glTF.
// - Every type here is an aggregate with no user-declared constructors.
//   Brace initialization is used at hundreds of call sites and turning these
//   into classes would break all of them.
//
// Vec2/Vec3/Vec4 and the grid types predate this file's expansion; their
// layout is unchanged.

// ---------------------------------------------------------------- scalars

inline constexpr float pi = 3.14159265358979323846f;

[[nodiscard]] constexpr float degreesToRadians(float degrees)
{
    return degrees * (pi / 180.0f);
}

[[nodiscard]] constexpr float radiansToDegrees(float radians)
{
    return radians * (180.0f / pi);
}

[[nodiscard]] constexpr float lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

// Absolute tolerance. Fine for the magnitudes this engine works in (board
// coordinates, normalized directions); not a general-purpose float compare.
[[nodiscard]] inline bool approximately(
    float left,
    float right,
    float tolerance = 1e-5f)
{
    return std::abs(left - right) <= tolerance;
}

// ---------------------------------------------------------------- vectors

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    friend constexpr bool operator==(Vec2, Vec2) = default;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend constexpr bool operator==(Vec3, Vec3) = default;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    friend constexpr bool operator==(Vec4, Vec4) = default;
};

[[nodiscard]] constexpr Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a) { return { -a.x, -a.y }; }
[[nodiscard]] constexpr Vec2 operator*(Vec2 a, float s) { return { a.x * s, a.y * s }; }
[[nodiscard]] constexpr Vec2 operator*(float s, Vec2 a) { return a * s; }
[[nodiscard]] constexpr Vec2 operator/(Vec2 a, float s) { return { a.x / s, a.y / s }; }
constexpr Vec2& operator+=(Vec2& a, Vec2 b) { a = a + b; return a; }
constexpr Vec2& operator-=(Vec2& a, Vec2 b) { a = a - b; return a; }
constexpr Vec2& operator*=(Vec2& a, float s) { a = a * s; return a; }

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 a) { return { -a.x, -a.y, -a.z }; }
[[nodiscard]] constexpr Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
[[nodiscard]] constexpr Vec3 operator*(float s, Vec3 a) { return a * s; }
[[nodiscard]] constexpr Vec3 operator/(Vec3 a, float s) { return { a.x / s, a.y / s, a.z / s }; }
constexpr Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
constexpr Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
constexpr Vec3& operator*=(Vec3& a, float s) { a = a * s; return a; }

[[nodiscard]] constexpr Vec4 operator+(Vec4 a, Vec4 b) { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
[[nodiscard]] constexpr Vec4 operator-(Vec4 a, Vec4 b) { return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
[[nodiscard]] constexpr Vec4 operator-(Vec4 a) { return { -a.x, -a.y, -a.z, -a.w }; }
[[nodiscard]] constexpr Vec4 operator*(Vec4 a, float s) { return { a.x * s, a.y * s, a.z * s, a.w * s }; }
[[nodiscard]] constexpr Vec4 operator*(float s, Vec4 a) { return a * s; }
[[nodiscard]] constexpr Vec4 operator/(Vec4 a, float s) { return { a.x / s, a.y / s, a.z / s, a.w / s }; }
constexpr Vec4& operator+=(Vec4& a, Vec4 b) { a = a + b; return a; }

// Named forms of the operators above.
//
// These exist because they are what the engine already says at several
// hundred call sites, and rewriting all of them to reach one definition would
// have been a large silent-transcription risk in projection code for a purely
// cosmetic gain. New code should prefer the operators, which read better in
// compound expressions; both spellings resolve to the same single definition,
// which is the part that matters.
[[nodiscard]] constexpr Vec2 add(Vec2 a, Vec2 b) { return a + b; }
[[nodiscard]] constexpr Vec3 add(Vec3 a, Vec3 b) { return a + b; }
[[nodiscard]] constexpr Vec4 add(Vec4 a, Vec4 b) { return a + b; }
[[nodiscard]] constexpr Vec2 subtract(Vec2 a, Vec2 b) { return a - b; }
[[nodiscard]] constexpr Vec3 subtract(Vec3 a, Vec3 b) { return a - b; }
[[nodiscard]] constexpr Vec4 subtract(Vec4 a, Vec4 b) { return a - b; }
[[nodiscard]] constexpr Vec2 multiply(Vec2 v, float s) { return v * s; }
[[nodiscard]] constexpr Vec3 multiply(Vec3 v, float s) { return v * s; }
[[nodiscard]] constexpr Vec4 multiply(Vec4 v, float s) { return v * s; }

[[nodiscard]] constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr float dot(Vec4 a, Vec4 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

// The z of the 3D cross product of two vectors in the xy plane. Its sign is
// the orientation of the turn a->b, which is what convex-hull and
// point-in-triangle tests actually want.
[[nodiscard]] constexpr float cross2D(Vec2 a, Vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] constexpr float lengthSquared(Vec2 v) { return dot(v, v); }
[[nodiscard]] constexpr float lengthSquared(Vec3 v) { return dot(v, v); }
[[nodiscard]] constexpr float lengthSquared(Vec4 v) { return dot(v, v); }

[[nodiscard]] inline float length(Vec2 v) { return std::sqrt(lengthSquared(v)); }
[[nodiscard]] inline float length(Vec3 v) { return std::sqrt(lengthSquared(v)); }
[[nodiscard]] inline float length(Vec4 v) { return std::sqrt(lengthSquared(v)); }

[[nodiscard]] inline float distance(Vec2 a, Vec2 b) { return length(a - b); }
[[nodiscard]] inline float distance(Vec3 a, Vec3 b) { return length(a - b); }

// Normalization has one policy, deliberately, because it used to have
// several. Before this module there were two normalize(Vec3) definitions with
// different epsilons (1e-6 and 1e-4) and different degenerate results
// ({0,0,1} and {0,0,0}). Merging them silently would have changed behaviour
// at whichever call sites relied on the other one.
//
// So: `normalize` returns the zero vector for input too short to have a
// direction, and a caller that needs a specific fallback says which one
// through `normalizeOr`. The threshold is on the *squared* length, so it is
// the same test regardless of which overload you are in.
inline constexpr float normalizeEpsilonSquared = 1e-12f;

[[nodiscard]] inline Vec2 normalizeOr(Vec2 v, Vec2 fallback)
{
    const float squared = lengthSquared(v);
    return squared <= normalizeEpsilonSquared ? fallback : v / std::sqrt(squared);
}

[[nodiscard]] inline Vec3 normalizeOr(Vec3 v, Vec3 fallback)
{
    const float squared = lengthSquared(v);
    return squared <= normalizeEpsilonSquared ? fallback : v / std::sqrt(squared);
}

[[nodiscard]] inline Vec4 normalizeOr(Vec4 v, Vec4 fallback)
{
    const float squared = lengthSquared(v);
    return squared <= normalizeEpsilonSquared ? fallback : v / std::sqrt(squared);
}

[[nodiscard]] inline Vec2 normalize(Vec2 v) { return normalizeOr(v, Vec2 {}); }
[[nodiscard]] inline Vec3 normalize(Vec3 v) { return normalizeOr(v, Vec3 {}); }
[[nodiscard]] inline Vec4 normalize(Vec4 v) { return normalizeOr(v, Vec4 {}); }

[[nodiscard]] constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
[[nodiscard]] constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
[[nodiscard]] constexpr Vec4 lerp(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }

[[nodiscard]] constexpr Vec3 minComponents(Vec3 a, Vec3 b)
{
    return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
}

[[nodiscard]] constexpr Vec3 maxComponents(Vec3 a, Vec3 b)
{
    return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
}

[[nodiscard]] inline Vec3 absComponents(Vec3 v)
{
    return { std::abs(v.x), std::abs(v.y), std::abs(v.z) };
}

[[nodiscard]] constexpr Vec3 xyz(Vec4 v) { return { v.x, v.y, v.z }; }
[[nodiscard]] constexpr Vec4 toVec4(Vec3 v, float w) { return { v.x, v.y, v.z, w }; }

// Rotates by X, then Y, then Z, each about the world axis.
//
// This exact sequence is load bearing: it is what the pre-existing model
// transforms used and what `model.vert.glsl` reproduces for normals, so the
// order and the signs here must not be "tidied".
[[nodiscard]] inline Vec3 rotateEulerXyz(Vec3 value, Vec3 radians)
{
    const float cosineX = std::cos(radians.x);
    const float sineX = std::sin(radians.x);
    value = {
        value.x,
        cosineX * value.y - sineX * value.z,
        sineX * value.y + cosineX * value.z,
    };

    const float cosineY = std::cos(radians.y);
    const float sineY = std::sin(radians.y);
    value = {
        cosineY * value.x + sineY * value.z,
        value.y,
        -sineY * value.x + cosineY * value.z,
    };

    const float cosineZ = std::cos(radians.z);
    const float sineZ = std::sin(radians.z);
    return {
        cosineZ * value.x - sineZ * value.y,
        sineZ * value.x + cosineZ * value.y,
        value.z,
    };
}

// ------------------------------------------------------------ grid types

struct GridPosition {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(GridPosition, GridPosition) = default;
};

struct GridPosition3 {
    int x = 0;
    int y = 0;
    int z = 0;

    friend constexpr bool operator==(GridPosition3, GridPosition3) = default;
};

[[nodiscard]] constexpr GridPosition xy(GridPosition3 position)
{
    return { position.x, position.y };
}

// ------------------------------------------------------------ quaternions

// Unit quaternion, (x, y, z, w) with w last to match glTF's storage order.
//
// Nothing here normalizes defensively on every operation; composing rotations
// drifts slowly and callers renormalize when they care. `slerp` is the
// exception, because it is meaningless on non-unit input.
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    friend constexpr bool operator==(Quat, Quat) = default;
};

inline constexpr Quat quatIdentity {};

[[nodiscard]] constexpr Quat quatFromVec4(Vec4 v) { return { v.x, v.y, v.z, v.w }; }
[[nodiscard]] constexpr Vec4 toVec4(Quat q) { return { q.x, q.y, q.z, q.w }; }

[[nodiscard]] constexpr float dot(Quat a, Quat b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] constexpr Quat operator-(Quat q) { return { -q.x, -q.y, -q.z, -q.w }; }

[[nodiscard]] inline Quat normalize(Quat q)
{
    const float squared = dot(q, q);
    return squared <= normalizeEpsilonSquared
        ? quatIdentity
        : Quat { q.x / std::sqrt(squared), q.y / std::sqrt(squared),
              q.z / std::sqrt(squared), q.w / std::sqrt(squared) };
}

[[nodiscard]] constexpr Quat conjugate(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }

// Composition: `a * b` applies b first, matching the matrix convention above.
[[nodiscard]] constexpr Quat operator*(Quat a, Quat b)
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

[[nodiscard]] inline Quat quatFromAxisAngle(Vec3 axis, float radians)
{
    const Vec3 unit = normalizeOr(axis, Vec3 { 0.0f, 0.0f, 1.0f });
    const float half = radians * 0.5f;
    const float sine = std::sin(half);
    return { unit.x * sine, unit.y * sine, unit.z * sine, std::cos(half) };
}

// Matches rotateEulerXyz: X first, then Y, then Z, about world axes.
[[nodiscard]] inline Quat quatFromEulerXyz(Vec3 radians)
{
    return quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, radians.z) *
        quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians.y) *
        quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, radians.x);
}

[[nodiscard]] inline Vec3 rotate(Quat q, Vec3 v)
{
    const Vec3 axis { q.x, q.y, q.z };
    const Vec3 t = cross(axis, v) * 2.0f;
    return v + t * q.w + cross(axis, t);
}

// Shortest-arc interpolation.
//
// Deliberately does NOT clamp t: this is the mathematical operation, and a
// caller that wants clamping says so. One of the implementations this
// replaced clamped and the other did not, so leaving it implicit would have
// changed behaviour at half the call sites without saying which half.
[[nodiscard]] inline Quat slerp(Quat from, Quat to, float t)
{
    from = normalize(from);
    to = normalize(to);
    float cosine = dot(from, to);
    if (cosine < 0.0f) {
        to = -to;
        cosine = -cosine;
    }
    // Near-parallel: sin(angle) underflows and the general form loses all
    // precision, so fall back to a normalized straight line.
    if (cosine > 0.9995f) {
        return normalize(Quat {
            lerp(from.x, to.x, t),
            lerp(from.y, to.y, t),
            lerp(from.z, to.z, t),
            lerp(from.w, to.w, t),
        });
    }
    const float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));
    const float sine = std::sin(angle);
    const float fromWeight = std::sin((1.0f - t) * angle) / sine;
    const float toWeight = std::sin(t * angle) / sine;
    return {
        from.x * fromWeight + to.x * toWeight,
        from.y * fromWeight + to.y * toWeight,
        from.z * fromWeight + to.z * toWeight,
        from.w * fromWeight + to.w * toWeight,
    };
}

// --------------------------------------------------------------- matrices

// Column-major 3x3: values[column * 3 + row].
struct Mat3 {
    std::array<float, 9> values { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    friend constexpr bool operator==(const Mat3&, const Mat3&) = default;
};

// Column-major 4x4: values[column * 4 + row]. Directly uploadable to GLSL.
struct Mat4 {
    std::array<float, 16> values {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    friend constexpr bool operator==(const Mat4&, const Mat4&) = default;
};

static_assert(sizeof(Mat4) == 64, "Mat4 must stay a bare 16-float block");
static_assert(sizeof(Mat3) == 36, "Mat3 must stay a bare 9-float block");

inline constexpr Mat3 mat3Identity {};
inline constexpr Mat4 mat4Identity {};

[[nodiscard]] constexpr float at(const Mat4& m, int row, int column)
{
    return m.values[static_cast<std::size_t>(column) * 4 + static_cast<std::size_t>(row)];
}

[[nodiscard]] constexpr float at(const Mat3& m, int row, int column)
{
    return m.values[static_cast<std::size_t>(column) * 3 + static_cast<std::size_t>(row)];
}

[[nodiscard]] constexpr Mat4 operator*(const Mat4& left, const Mat4& right)
{
    Mat4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int index = 0; index < 4; ++index) {
                value += at(left, row, index) * at(right, index, column);
            }
            result.values[static_cast<std::size_t>(column) * 4 +
                static_cast<std::size_t>(row)] = value;
        }
    }
    return result;
}

[[nodiscard]] constexpr Mat3 operator*(const Mat3& left, const Mat3& right)
{
    Mat3 result {};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            float value = 0.0f;
            for (int index = 0; index < 3; ++index) {
                value += at(left, row, index) * at(right, index, column);
            }
            result.values[static_cast<std::size_t>(column) * 3 +
                static_cast<std::size_t>(row)] = value;
        }
    }
    return result;
}

[[nodiscard]] constexpr Mat4 transpose(const Mat4& m)
{
    Mat4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result.values[static_cast<std::size_t>(column) * 4 +
                static_cast<std::size_t>(row)] = at(m, column, row);
        }
    }
    return result;
}

[[nodiscard]] constexpr Mat3 transpose(const Mat3& m)
{
    Mat3 result {};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            result.values[static_cast<std::size_t>(column) * 3 +
                static_cast<std::size_t>(row)] = at(m, column, row);
        }
    }
    return result;
}

// Upper-left 3x3, i.e. the rotation and scale without the translation.
[[nodiscard]] constexpr Mat3 linearPart(const Mat4& m)
{
    Mat3 result {};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            result.values[static_cast<std::size_t>(column) * 3 +
                static_cast<std::size_t>(row)] = at(m, row, column);
        }
    }
    return result;
}

[[nodiscard]] constexpr float determinant(const Mat3& m)
{
    return at(m, 0, 0) * (at(m, 1, 1) * at(m, 2, 2) - at(m, 1, 2) * at(m, 2, 1)) -
        at(m, 0, 1) * (at(m, 1, 0) * at(m, 2, 2) - at(m, 1, 2) * at(m, 2, 0)) +
        at(m, 0, 2) * (at(m, 1, 0) * at(m, 2, 1) - at(m, 1, 1) * at(m, 2, 0));
}

// Returns identity for a singular matrix rather than infinities, so a
// degenerate transform produces something drawable instead of NaN geometry
// that propagates into bounds and culling.
[[nodiscard]] inline Mat3 inverse(const Mat3& m)
{
    const float det = determinant(m);
    if (std::abs(det) <= 1e-12f) {
        return mat3Identity;
    }
    const float invDet = 1.0f / det;
    Mat3 result {};
    const auto set = [&result](int row, int column, float value) {
        result.values[static_cast<std::size_t>(column) * 3 +
            static_cast<std::size_t>(row)] = value;
    };
    set(0, 0, (at(m, 1, 1) * at(m, 2, 2) - at(m, 2, 1) * at(m, 1, 2)) * invDet);
    set(0, 1, (at(m, 0, 2) * at(m, 2, 1) - at(m, 0, 1) * at(m, 2, 2)) * invDet);
    set(0, 2, (at(m, 0, 1) * at(m, 1, 2) - at(m, 0, 2) * at(m, 1, 1)) * invDet);
    set(1, 0, (at(m, 1, 2) * at(m, 2, 0) - at(m, 1, 0) * at(m, 2, 2)) * invDet);
    set(1, 1, (at(m, 0, 0) * at(m, 2, 2) - at(m, 0, 2) * at(m, 2, 0)) * invDet);
    set(1, 2, (at(m, 1, 0) * at(m, 0, 2) - at(m, 0, 0) * at(m, 1, 2)) * invDet);
    set(2, 0, (at(m, 1, 0) * at(m, 2, 1) - at(m, 2, 0) * at(m, 1, 1)) * invDet);
    set(2, 1, (at(m, 2, 0) * at(m, 0, 1) - at(m, 0, 0) * at(m, 2, 1)) * invDet);
    set(2, 2, (at(m, 0, 0) * at(m, 1, 1) - at(m, 1, 0) * at(m, 0, 1)) * invDet);
    return result;
}

// General 4x4 inverse (cofactor expansion). Also returns identity when
// singular, for the same reason as the 3x3.
[[nodiscard]] inline Mat4 inverse(const Mat4& matrix)
{
    const std::array<float, 16>& m = matrix.values;
    std::array<float, 16> inv {};

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
        m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
        m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
        m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
        m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
        m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
        m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
        m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
        m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
        m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
        m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
        m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
        m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
        m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
        m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
        m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
        m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::abs(det) <= 1e-20f) {
        return mat4Identity;
    }
    const float invDet = 1.0f / det;
    Mat4 result {};
    for (std::size_t index = 0; index < inv.size(); ++index) {
        result.values[index] = inv[index] * invDet;
    }
    return result;
}

[[nodiscard]] constexpr Vec4 transform(const Mat4& m, Vec4 v)
{
    return {
        at(m, 0, 0) * v.x + at(m, 0, 1) * v.y + at(m, 0, 2) * v.z + at(m, 0, 3) * v.w,
        at(m, 1, 0) * v.x + at(m, 1, 1) * v.y + at(m, 1, 2) * v.z + at(m, 1, 3) * v.w,
        at(m, 2, 0) * v.x + at(m, 2, 1) * v.y + at(m, 2, 2) * v.z + at(m, 2, 3) * v.w,
        at(m, 3, 0) * v.x + at(m, 3, 1) * v.y + at(m, 3, 2) * v.z + at(m, 3, 3) * v.w,
    };
}

// Point: translation applies. Vector/direction: it does not.
[[nodiscard]] constexpr Vec3 transformPoint(const Mat4& m, Vec3 p)
{
    return xyz(transform(m, toVec4(p, 1.0f)));
}

[[nodiscard]] constexpr Vec3 transformVector(const Mat4& m, Vec3 v)
{
    return xyz(transform(m, toVec4(v, 0.0f)));
}

[[nodiscard]] constexpr Vec3 transform(const Mat3& m, Vec3 v)
{
    return {
        at(m, 0, 0) * v.x + at(m, 0, 1) * v.y + at(m, 0, 2) * v.z,
        at(m, 1, 0) * v.x + at(m, 1, 1) * v.y + at(m, 1, 2) * v.z,
        at(m, 2, 0) * v.x + at(m, 2, 1) * v.y + at(m, 2, 2) * v.z,
    };
}

// The matrix that transforms normals: inverse-transpose of the linear part.
// Only differs from the linear part under non-uniform scale, which is exactly
// when getting it wrong is visible.
[[nodiscard]] inline Mat3 normalMatrix(const Mat4& m)
{
    return transpose(inverse(linearPart(m)));
}

[[nodiscard]] inline Mat4 mat4FromQuat(Quat q)
{
    q = normalize(q);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    Mat4 result {};
    result.values[0] = 1.0f - 2.0f * (yy + zz);
    result.values[1] = 2.0f * (xy + wz);
    result.values[2] = 2.0f * (xz - wy);
    result.values[4] = 2.0f * (xy - wz);
    result.values[5] = 1.0f - 2.0f * (xx + zz);
    result.values[6] = 2.0f * (yz + wx);
    result.values[8] = 2.0f * (xz + wy);
    result.values[9] = 2.0f * (yz - wx);
    result.values[10] = 1.0f - 2.0f * (xx + yy);
    return result;
}

[[nodiscard]] inline Mat4 mat4FromTrs(Vec3 translation, Quat rotation, Vec3 scale)
{
    Mat4 result = mat4FromQuat(rotation);
    for (std::size_t row = 0; row < 3; ++row) {
        result.values[row] *= scale.x;
        result.values[4 + row] *= scale.y;
        result.values[8 + row] *= scale.z;
    }
    result.values[12] = translation.x;
    result.values[13] = translation.y;
    result.values[14] = translation.z;
    return result;
}

[[nodiscard]] constexpr Mat4 mat4FromTranslation(Vec3 t)
{
    Mat4 result {};
    result.values[12] = t.x;
    result.values[13] = t.y;
    result.values[14] = t.z;
    return result;
}

[[nodiscard]] constexpr Mat4 mat4FromScale(Vec3 s)
{
    Mat4 result {};
    result.values[0] = s.x;
    result.values[5] = s.y;
    result.values[10] = s.z;
    return result;
}

// ---------------------------------------------------------------- cameras

// View matrix from an orthonormal camera basis.
//
// Conventions here are the renderer's, and they are not the OpenGL ones: the
// camera looks down **+z**, not -z, and there is no Y flip. The scene pass
// draws through a negative-height viewport, which is what puts +y up in
// Vulkan's y-down NDC, so a matrix that flipped y as well would flip it back.
//
// (right, up, forward) must be orthonormal and right-handed - right x up ==
// forward. That is what lets the rotation be inverted by transposing it,
// which is all this does: the rows are the basis vectors and the translation
// column is the eye projected onto them.
[[nodiscard]] inline Mat4 mat4View(Vec3 eye, Vec3 right, Vec3 up, Vec3 forward)
{
    Mat4 result {};
    result.values[0] = right.x;
    result.values[4] = right.y;
    result.values[8] = right.z;
    result.values[1] = up.x;
    result.values[5] = up.y;
    result.values[9] = up.z;
    result.values[2] = forward.x;
    result.values[6] = forward.y;
    result.values[10] = forward.z;
    result.values[12] = -dot(right, eye);
    result.values[13] = -dot(up, eye);
    result.values[14] = -dot(forward, eye);
    return result;
}

// Perspective projection into Vulkan's clip volume: 0 <= z <= w, +z forward.
//
// `focalLength` is 1 / tan(verticalFov / 2) - the distance to a view plane one
// unit tall - and `aspect` is width / height. `centerOffset` shears the frustum
// off axis: the point that lands in the middle of the screen is the one whose
// projected position equals it. A symmetric frustum passes {0, 0}. `scale`
// multiplies x and y after projection, which is how a fit-to-content camera
// zooms without touching the field of view or the depth mapping.
//
// The depth row is the ordinary Vulkan one. Written out, z/w works out to
// far * (viewZ - near) / ((far - near) * viewZ), which is 0 at the near plane
// and 1 at the far plane.
[[nodiscard]] inline Mat4 mat4PerspectiveOffCenter(
    float focalLength,
    float aspect,
    float nearZ,
    float farZ,
    Vec2 centerOffset,
    float scale)
{
    const float safeAspect = std::abs(aspect) > 1e-6f ? aspect : 1.0f;
    const float safeNear = std::max(nearZ, 1e-4f);
    const float safeFar = std::max(farZ, safeNear + 1e-4f);
    const float depthRange = safeFar - safeNear;

    // Column-major, so each line below is a column, not a row. The 1 in
    // column 2 row 3 is what puts view-space z into w.
    return Mat4 { {
        scale * focalLength / safeAspect, 0.0f, 0.0f, 0.0f,
        0.0f, scale * focalLength, 0.0f, 0.0f,
        -scale * centerOffset.x, -scale * centerOffset.y,
            safeFar / depthRange, 1.0f,
        0.0f, 0.0f, -safeFar * safeNear / depthRange, 0.0f,
    } };
}

// --------------------------------------------------------------- transform

// Translation, rotation and scale as separable parts. Composing here rather
// than multiplying matrices keeps the scale extractable, which matters for
// bounds and for normal matrices.
struct Transform {
    Vec3 translation {};
    Quat rotation {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

[[nodiscard]] inline Mat4 toMat4(const Transform& t)
{
    return mat4FromTrs(t.translation, t.rotation, t.scale);
}

[[nodiscard]] inline Vec3 transformPoint(const Transform& t, Vec3 p)
{
    return t.translation +
        rotate(t.rotation, Vec3 { p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z });
}

} // namespace sokoban

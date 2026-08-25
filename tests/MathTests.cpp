#include "engine/Math.hpp"

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

void checkNear(
    sokoban::Vec3 actual,
    sokoban::Vec3 expected,
    const char* label,
    float tolerance = 1e-5f)
{
    checkNear(actual.x, expected.x, label, tolerance);
    checkNear(actual.y, expected.y, label, tolerance);
    checkNear(actual.z, expected.z, label, tolerance);
}

using namespace sokoban;

void checkNear(const Mat4& actual, const Mat4& expected, const char* label,
    float tolerance = 1e-5f);

void testVectorAlgebra()
{
    const Vec3 a { 1.0f, 2.0f, 3.0f };
    const Vec3 b { 4.0f, -5.0f, 6.0f };
    check(a + b == Vec3 { 5.0f, -3.0f, 9.0f }, "vec3 add");
    check(a - b == Vec3 { -3.0f, 7.0f, -3.0f }, "vec3 subtract");
    check(a * 2.0f == Vec3 { 2.0f, 4.0f, 6.0f }, "vec3 scale");
    check(2.0f * a == a * 2.0f, "scalar multiply commutes");
    check(-a == Vec3 { -1.0f, -2.0f, -3.0f }, "vec3 negate");
    checkNear(dot(a, b), 4.0f - 10.0f + 18.0f, "vec3 dot");

    // Right-handed: x cross y is +z.
    const Vec3 crossed = cross({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    checkNear(crossed, { 0.0f, 0.0f, 1.0f }, "cross is right-handed");
    checkNear(dot(cross(a, b), a), 0.0f, "cross is perpendicular to a", 1e-4f);
    checkNear(dot(cross(a, b), b), 0.0f, "cross is perpendicular to b", 1e-4f);

    checkNear(length(Vec3 { 3.0f, 4.0f, 0.0f }), 5.0f, "vec3 length");
    checkNear(length(normalize(Vec3 { 3.0f, 4.0f, 0.0f })), 1.0f, "normalize is unit");
    checkNear(distance(Vec2 { 1.0f, 1.0f }, Vec2 { 4.0f, 5.0f }), 5.0f, "vec2 distance");

    // Orientation sign, which is what the hull and triangle tests rely on.
    check(cross2D({ 1.0f, 0.0f }, { 0.0f, 1.0f }) > 0.0f, "cross2D left turn positive");
    check(cross2D({ 0.0f, 1.0f }, { 1.0f, 0.0f }) < 0.0f, "cross2D right turn negative");

    checkNear(lerp(Vec3 { 0.0f, 0.0f, 0.0f }, Vec3 { 10.0f, 20.0f, 30.0f }, 0.25f),
        { 2.5f, 5.0f, 7.5f }, "vec3 lerp");
}

void testDegenerateNormalization()
{
    // The policy that replaced two conflicting ones. `normalize` yields zero;
    // a caller wanting a specific axis back asks for it.
    check(normalize(Vec3 {}) == Vec3 {}, "degenerate normalize is zero");
    check(normalizeOr(Vec3 {}, { 0.0f, 0.0f, 1.0f }) == Vec3 { 0.0f, 0.0f, 1.0f },
        "degenerate normalizeOr uses the fallback");
    // Just above the threshold: still normalized, not swallowed.
    const Vec3 tiny { 1e-5f, 0.0f, 0.0f };
    checkNear(length(normalize(tiny)), 1.0f, "a small but real vector still normalizes");
    check(normalize(Vec3 { 1e-7f, 0.0f, 0.0f }) == Vec3 {},
        "below the threshold is treated as directionless");
}

void testQuaternionBasics()
{
    checkNear(rotate(quatIdentity, { 1.0f, 2.0f, 3.0f }), { 1.0f, 2.0f, 3.0f },
        "identity rotation is a no-op");

    const Quat quarterZ = quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, pi * 0.5f);
    checkNear(rotate(quarterZ, { 1.0f, 0.0f, 0.0f }), { 0.0f, 1.0f, 0.0f },
        "quarter turn about z maps +x to +y");

    // Composition order: a * b applies b first, matching the matrix rule.
    const Quat quarterX = quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, pi * 0.5f);
    const Vec3 composed = rotate(quarterZ * quarterX, { 0.0f, 1.0f, 0.0f });
    const Vec3 sequential = rotate(quarterZ, rotate(quarterX, { 0.0f, 1.0f, 0.0f }));
    checkNear(composed, sequential, "quaternion composition applies right operand first");

    const Quat q = quatFromAxisAngle({ 1.0f, 2.0f, 3.0f }, 0.7f);
    checkNear(rotate(q * conjugate(q), { 1.0f, 2.0f, 3.0f }), { 1.0f, 2.0f, 3.0f },
        "q times its conjugate is identity");
    checkNear(std::sqrt(dot(q, q)), 1.0f, "axis-angle produces a unit quaternion");
    // Rotation preserves length.
    checkNear(length(rotate(q, { 3.0f, 0.0f, 4.0f })), 5.0f, "rotation is length preserving");
}

// The most load-bearing test here. rotateEulerXyz is the pre-existing
// convention baked into model transforms and reproduced in model.vert.glsl.
// If the quaternion path disagrees with it, anything that migrates from one
// to the other silently rotates geometry wrongly.
void testQuaternionMatchesLegacyEuler()
{
    const Vec3 samples[] = {
        { 0.0f, 0.0f, 0.0f },
        { pi * 0.5f, 0.0f, 0.0f },
        { 0.0f, pi * 0.5f, 0.0f },
        { 0.0f, 0.0f, pi * 0.5f },
        { 0.3f, -0.7f, 1.9f },
        { -2.1f, 0.4f, -0.9f },
    };
    const Vec3 points[] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.4f, -1.3f, 2.2f },
    };
    for (Vec3 radians : samples) {
        const Quat q = quatFromEulerXyz(radians);
        for (Vec3 point : points) {
            checkNear(rotate(q, point), rotateEulerXyz(point, radians),
                "quatFromEulerXyz matches rotateEulerXyz", 1e-4f);
        }
        // And the matrix form must agree with both.
        const Mat4 m = mat4FromQuat(q);
        for (Vec3 point : points) {
            checkNear(transformVector(m, point), rotateEulerXyz(point, radians),
                "mat4FromQuat matches rotateEulerXyz", 1e-4f);
        }
    }
}

void testSlerp()
{
    const Quat from = quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, 0.0f);
    const Quat to = quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, pi * 0.5f);
    checkNear(rotate(slerp(from, to, 0.0f), { 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f },
        "slerp at 0 is the start");
    checkNear(rotate(slerp(from, to, 1.0f), { 1.0f, 0.0f, 0.0f }), { 0.0f, 1.0f, 0.0f },
        "slerp at 1 is the end");
    const Vec3 half = rotate(slerp(from, to, 0.5f), { 1.0f, 0.0f, 0.0f });
    checkNear(half, { std::cos(pi * 0.25f), std::sin(pi * 0.25f), 0.0f },
        "slerp midpoint is the half angle");

    const Quat interpolated = slerp(from, to, 0.37f);
    checkNear(std::sqrt(dot(interpolated, interpolated)), 1.0f, "slerp stays unit");

    // Shortest arc: negating one input must not change the path taken.
    const Vec3 viaNegated = rotate(slerp(from, -to, 0.5f), { 1.0f, 0.0f, 0.0f });
    checkNear(viaNegated, half, "slerp takes the shortest arc regardless of sign");

    // Near-parallel inputs go through the linear fallback and must still be
    // unit and close to the endpoints.
    const Quat nearly = quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, 1e-4f);
    const Quat blended = slerp(from, nearly, 0.5f);
    checkNear(std::sqrt(dot(blended, blended)), 1.0f, "near-parallel slerp stays unit");
}

void testMatrixLayoutAndAlgebra()
{
    // Column-major: a translation lands in elements 12..14, which is what
    // makes a Mat4 uploadable to GLSL without transposing.
    const Mat4 translation = mat4FromTranslation({ 5.0f, 6.0f, 7.0f });
    checkNear(translation.values[12], 5.0f, "translation x at index 12");
    checkNear(translation.values[13], 6.0f, "translation y at index 13");
    checkNear(translation.values[14], 7.0f, "translation z at index 14");

    check(mat4Identity * mat4Identity == mat4Identity, "identity squared is identity");
    checkNear(transformPoint(mat4Identity, { 1.0f, 2.0f, 3.0f }), { 1.0f, 2.0f, 3.0f },
        "identity transform is a no-op");
    checkNear(transformPoint(translation, { 1.0f, 1.0f, 1.0f }), { 6.0f, 7.0f, 8.0f },
        "a point picks up translation");
    checkNear(transformVector(translation, { 1.0f, 1.0f, 1.0f }), { 1.0f, 1.0f, 1.0f },
        "a vector does not pick up translation");

    // Right-to-left composition: scale first, then translate.
    const Mat4 scale = mat4FromScale({ 2.0f, 2.0f, 2.0f });
    checkNear(transformPoint(translation * scale, { 1.0f, 1.0f, 1.0f }),
        { 7.0f, 8.0f, 9.0f }, "a * b applies b first");
    checkNear(transformPoint(scale * translation, { 1.0f, 1.0f, 1.0f }),
        { 12.0f, 14.0f, 16.0f }, "the other order scales the translation too");

    const Mat4 rotation = mat4FromQuat(quatFromAxisAngle({ 0.3f, 1.0f, -0.2f }, 1.1f));
    const Mat4 composed = translation * rotation * scale;
    checkNear(transpose(transpose(composed)), composed, "transpose is an involution", 1e-5f);
}

void checkNear(const Mat4& actual, const Mat4& expected, const char* label, float tolerance)
{
    for (std::size_t index = 0; index < actual.values.size(); ++index) {
        checkNear(actual.values[index], expected.values[index], label, tolerance);
    }
}

void testInverse()
{
    const Mat4 m = mat4FromTrs(
        { 3.0f, -4.0f, 5.0f },
        quatFromAxisAngle({ 1.0f, 2.0f, 3.0f }, 0.9f),
        { 2.0f, 0.5f, 1.5f });
    const Mat4 roundTrip = m * inverse(m);
    checkNear(roundTrip, mat4Identity, "m * inverse(m) is identity", 1e-4f);

    const Vec3 point { 1.3f, -2.7f, 0.4f };
    checkNear(transformPoint(inverse(m), transformPoint(m, point)), point,
        "inverse undoes the transform", 1e-3f);

    // Singular input returns identity rather than infinities, so degenerate
    // geometry stays drawable instead of becoming NaN.
    Mat4 singular = mat4Identity;
    singular.values[0] = 0.0f;
    singular.values[5] = 0.0f;
    singular.values[10] = 0.0f;
    check(inverse(singular) == mat4Identity, "a singular matrix inverts to identity");
}

void testTrsAndNormalMatrix()
{
    const Vec3 translation { 1.0f, 2.0f, 3.0f };
    const Quat rotation = quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, pi * 0.5f);
    const Vec3 scale { 2.0f, 3.0f, 4.0f };
    const Mat4 m = mat4FromTrs(translation, rotation, scale);

    // Scale, then rotate, then translate.
    checkNear(transformPoint(m, { 1.0f, 0.0f, 0.0f }), { 1.0f, 4.0f, 3.0f },
        "TRS applies scale, rotation, then translation");

    const Transform t { translation, rotation, scale };
    checkNear(transformPoint(t, { 1.0f, 0.0f, 0.0f }), transformPoint(m, { 1.0f, 0.0f, 0.0f }),
        "Transform agrees with its matrix");
    checkNear(transformPoint(toMat4(t), { -0.5f, 1.5f, 2.0f }),
        transformPoint(t, { -0.5f, 1.5f, 2.0f }), "toMat4 agrees with Transform");

    // Under non-uniform scale a normal transformed by the linear part is no
    // longer perpendicular to the surface; the normal matrix is what fixes it.
    const Mat4 squash = mat4FromScale({ 4.0f, 1.0f, 1.0f });
    // Genuinely perpendicular to start with - dot((1,-1,0), (1,1,0)) == 0 -
    // and both off-axis, so the squash actually shears their relationship.
    const Vec3 tangent { 1.0f, -1.0f, 0.0f };
    const Vec3 normal { 1.0f, 1.0f, 0.0f };
    const Vec3 movedTangent = transformVector(squash, tangent);
    const Vec3 naive = transformVector(squash, normal);
    const Vec3 corrected = transform(normalMatrix(squash), normal);
    check(std::abs(dot(normalize(naive), normalize(movedTangent))) > 1e-3f,
        "the naive normal stops being perpendicular under non-uniform scale");
    checkNear(dot(normalize(corrected), normalize(movedTangent)), 0.0f,
        "the normal matrix keeps normals perpendicular", 1e-4f);
}

void testScalarHelpers()
{
    checkNear(degreesToRadians(180.0f), pi, "degrees to radians");
    checkNear(radiansToDegrees(pi), 180.0f, "radians to degrees");
    checkNear(lerp(10.0f, 20.0f, 0.5f), 15.0f, "scalar lerp");
    check(approximately(1.0f, 1.0f + 1e-6f), "approximately accepts tiny drift");
    check(!approximately(1.0f, 1.01f), "approximately rejects real differences");
}

} // namespace

int main()
{
    testVectorAlgebra();
    testDegenerateNormalization();
    testQuaternionBasics();
    testQuaternionMatchesLegacyEuler();
    testSlerp();
    testMatrixLayoutAndAlgebra();
    testInverse();
    testTrsAndNormalMatrix();
    testScalarHelpers();

    if (failures != 0) {
        std::cerr << "MathTests: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "MathTests passed\n";
    return 0;
}

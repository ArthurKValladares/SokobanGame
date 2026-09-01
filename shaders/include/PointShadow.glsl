#ifndef SOKOBAN_POINT_SHADOW_GLSL
#define SOKOBAN_POINT_SHADOW_GLSL

// Point-light shadowing, shared by the scene and ground shaders.
//
// These two carried the same function twice, and the copies had drifted: the
// scene averaged five taps a texel apart, the ground took one. Everything else
// - the near and far planes, the two early-outs, the grazing-angle bias, the
// cube-map depth conversion - was the same text in both, which is exactly what
// made the difference hard to see. It is one function now, and the tap count is
// the one thing a shader chooses.
//
// The split is deliberate and is being kept. Ground covers far more of the
// frame than the models and tiles do, and both shaders loop over up to eight
// point lights, so five taps on the ground would be up to forty cube-map
// samples on the largest surface in the scene against eight today. Whether
// that trade is worth making is a question for a capture, not for a cleanup;
// what this file changes is that the answer is now one token in each shader
// rather than a divergence you have to diff two files to find.
//
// Define POINT_SHADOW_TAPS before including this. Only 1 and 5 exist, because
// only those two shapes are in use - a general N-tap kernel would be inventing
// a knob nobody has asked for. The two sampling bodies are written out
// separately below rather than sharing a loop, for a measured reason stated
// there; everything before them, which is where the drift was, is shared.

#if !defined(POINT_SHADOW_TAPS)
#error "define POINT_SHADOW_TAPS (1 or 5) before including PointShadow.glsl"
#elif POINT_SHADOW_TAPS != 1 && POINT_SHADOW_TAPS != 5
#error "POINT_SHADOW_TAPS must be 1 or 5"
#endif

// The near plane the cube-face projections were built with.
//
// This is the other half of a pair: VulkanSceneRecorder builds each face's
// projection from config::pointShadowNearPlane, and the reconstruction below
// inverts it. If the two ever disagree the recovered distance is wrong by a
// constant, which shows up as acne or as shadows detaching from their caster -
// neither of which reads as a mismatched constant. The draw_mode suite pins
// them against each other.
const float POINT_SHADOW_NEAR_PLANE = 0.05;

// Cube-map depth back to a world-space distance from the light.
float pointShadowWorldDistance(
    float depth, float nearPlane, float farPlane)
{
    float denominator = farPlane - depth * (farPlane - nearPlane);
    return denominator > 0.000001
        ? farPlane * nearPlane / denominator
        : farPlane;
}

float pointShadowFactor(
    int lightIndex, vec3 fromLight, vec3 surfaceNormal)
{
    PointLightData light = frame.pointLights[lightIndex];
    if (light.shadowOptions.x <= 0.5) {
        return 1.0;
    }
    const float nearPlane = POINT_SHADOW_NEAR_PLANE;
    float farPlane = max(light.positionAndRange.w, nearPlane + 0.001);
    float majorDistance = max(
        abs(fromLight.x), max(abs(fromLight.y), abs(fromLight.z)));
    if (majorDistance <= nearPlane || majorDistance >= farPlane) {
        return 1.0;
    }
    vec3 direction = normalize(fromLight);
    // The authored bias is a world-space minimum. Increase it at grazing
    // angles, where rasterized depth changes fastest across the surface.
    float facing = clamp(dot(normalize(surfaceNormal), -direction), 0.0, 1.0);
    float worldBias = max(light.shadowOptions.z, 0.0) *
        (1.0 + 2.0 * (1.0 - facing));

    float shadowed = 0.0;
#if POINT_SHADOW_TAPS == 5
    // A cross one texel wide, in the plane facing the light.
    float texelAngle = 2.0 / float(textureSize(pointShadowMaps, 0).x);
    vec3 tangent = normalize(cross(
        abs(direction.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
                               : vec3(0.0, 1.0, 0.0),
        direction));
    vec3 bitangent = cross(direction, tangent);
    vec3 offsets[5] = vec3[5](
        vec3(0.0), tangent, -tangent, bitangent, -bitangent);
    for (int sampleIndex = 0; sampleIndex < 5; ++sampleIndex) {
        vec3 sampleDirection = direction +
            offsets[sampleIndex] * texelAngle;
        float closestDepth = texture(
            pointShadowMaps,
            vec4(sampleDirection, light.shadowOptions.y)).r;
        float closestDistance = pointShadowWorldDistance(
            closestDepth, nearPlane, farPlane);
        shadowed += majorDistance - worldBias > closestDistance
            ? 1.0
            : 0.0;
    }
#else
    // Written out rather than run as a one-iteration loop. glslc does not
    // unroll that loop, so the loop form costs the ground a real compare,
    // branch and phi per fragment per light - which is the opposite of why
    // the ground takes one tap. Measured: the loop form is 204 bytes and 14
    // instructions larger.
    float closestDepth = texture(
        pointShadowMaps,
        vec4(direction, light.shadowOptions.y)).r;
    float closestDistance = pointShadowWorldDistance(
        closestDepth, nearPlane, farPlane);
    shadowed = majorDistance - worldBias > closestDistance
        ? 1.0
        : 0.0;
#endif

#if POINT_SHADOW_TAPS == 5
    return 1.0 - shadowed / 5.0 *
        clamp(light.shadowOptions.w, 0.0, 1.0);
#else
    return 1.0 - shadowed * clamp(light.shadowOptions.w, 0.0, 1.0);
#endif
}

#endif

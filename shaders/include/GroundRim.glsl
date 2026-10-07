#ifndef SOKOBAN_GROUND_RIM_GLSL
#define SOKOBAN_GROUND_RIM_GLSL

// Mirrors GroundRimGeometry.cpp. params=(width,depth,bodyBand,exposedSides);
// concave corner bits are NW=1, NE=2, SE=4, SW=8. Width zero disables the
// treatment without changing the shared source vertices or index variants.
struct GroundRimSample
{
    float drop;
    vec2 gradient;
};

GroundRimSample groundRimEdgeRamp(float distance, vec2 gradient, float width)
{
    float raw = 1.0 - distance / width;
    return GroundRimSample(
        clamp(raw, 0.0, 1.0),
        raw >= 0.0 && raw <= 1.0 ? -gradient / width : vec2(0.0));
}

bool groundRimProfileValid(vec4 params)
{
    return !any(isnan(params)) && !any(isinf(params)) &&
        params.x > 0.0 && params.x <= 0.5 && params.y > 0.0 &&
        params.z > params.y && params.z <= 1.0;
}

GroundRimSample groundRimSample(vec2 position, vec4 params, uint corners)
{
    GroundRimSample result = GroundRimSample(0.0, vec2(0.0));
    if (!groundRimProfileValid(params) || any(isnan(position)) || any(isinf(position))) {
        return result;
    }
    GroundRimSample edges[4] = GroundRimSample[4](
        groundRimEdgeRamp(position.y, vec2(0.0, 1.0), params.x),
        groundRimEdgeRamp(1.0-position.x, vec2(-1.0, 0.0), params.x),
        groundRimEdgeRamp(1.0-position.y, vec2(0.0, -1.0), params.x),
        groundRimEdgeRamp(position.x, vec2(1.0, 0.0), params.x));
    uint sides = uint(params.w + 0.5);
    for (uint side = 0u; side < 4u; ++side) {
        if ((sides & (1u << side)) != 0u && edges[side].drop > result.drop) {
            result = edges[side];
        }
    }
    const uvec2 adjacent[4] = uvec2[4](
        uvec2(0u,3u), uvec2(0u,1u), uvec2(2u,1u), uvec2(2u,3u));
    for (uint corner = 0u; corner < 4u; ++corner) {
        if ((corners & (1u << corner)) == 0u) {
            continue;
        }
        GroundRimSample a = edges[adjacent[corner].x];
        GroundRimSample b = edges[adjacent[corner].y];
        GroundRimSample candidate = a.drop <= b.drop ? a : b;
        if (candidate.drop > result.drop) {
            result = candidate;
        }
    }
    result.drop *= params.y;
    result.gradient *= params.y;
    return result;
}

struct GroundRimDeformation
{
    vec3 position;
    vec3 jacobian;
};

GroundRimDeformation groundRimDeformation(vec3 position, vec4 params, uint corners)
{
    GroundRimDeformation result = GroundRimDeformation(position, vec3(0.0,0.0,1.0));
    if (isnan(position.z) || isinf(position.z)) {
        return result;
    }
    GroundRimSample rimSample = groundRimSample(position.xy, params, corners);
    if (rimSample.drop <= 0.0) {
        return result;
    }
    float rawBand = (position.z - (1.0-params.z)) / params.z;
    float band = clamp(rawBand, 0.0, 1.0);
    if (band <= 0.0) {
        return result;
    }
    float derivative = rawBand <= 1.0 ? 1.0/params.z : 0.0;
    result.position.z -= band * rimSample.drop;
    result.jacobian = vec3(-band * rimSample.gradient, 1.0-derivative * rimSample.drop);
    return result;
}

vec3 groundRimDeformNormal(vec3 normal, GroundRimDeformation deformation)
{
    float normalZ = normal.z / deformation.jacobian.z;
    return vec3(normal.xy - deformation.jacobian.xy * normalZ, normalZ);
}

vec3 groundRimDeformTangent(vec3 tangent, GroundRimDeformation deformation)
{
    return vec3(tangent.xy, dot(deformation.jacobian, tangent));
}

#endif

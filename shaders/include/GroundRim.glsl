#ifndef SOKOBAN_GROUND_RIM_GLSL
#define SOKOBAN_GROUND_RIM_GLSL

// Mirrors GroundRimGeometry.cpp. params=(width,depth,bodyBand,exposedSides);
// origin is the world tile origin; corner bits are NW=1, NE=2, SE=4, SW=8.
// Caps and body vertices interpolate the same broad fracture facets. Exposed
// borders remain affine between corners, sealing every authored wall segment.
struct GroundRimSample
{
    float drop;
    vec2 gradient;
};

const vec2 groundRimCornerOffsets[4] = vec2[4](
    vec2(0.0,0.0), vec2(1.0,0.0), vec2(1.0,1.0), vec2(0.0,1.0));
const uvec2 groundRimSideCorners[4] = uvec2[4](
    uvec2(0u,1u), uvec2(1u,2u), uvec2(3u,2u), uvec2(0u,3u));

uint groundRimFractureHash(vec2 point, uint salt)
{
    // Canonicalize negative zero before hashing the exact float bits.
    uint hash = floatBitsToUint(point.x == 0.0 ? 0.0 : point.x) * 1664525u ^
        floatBitsToUint(point.y == 0.0 ? 0.0 : point.y) * 1013904223u ^ salt;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    return hash ^ (hash >> 16);
}

float groundRimFractureUnit(vec2 point, uint salt)
{
    return float(groundRimFractureHash(point, salt) & 65535u) / 65535.0;
}

struct GroundRimCorner
{
    float width;
    float drop;
};

GroundRimCorner groundRimCornerFor(uint corner, vec4 params, vec2 origin)
{
    vec2 world = origin + groundRimCornerOffsets[corner];
    return GroundRimCorner(
        min(0.40, params.x * (0.60 + 0.75 * groundRimFractureUnit(world, 17u))),
        params.y * (0.55 + 0.80 * groundRimFractureUnit(world, 29u)));
}

struct GroundRimQuad
{
    vec3 vertices[4];
};

GroundRimQuad groundRimCornerQuad(uint corner, vec4 params, uint corners, vec2 origin)
{
    GroundRimCorner shape = groundRimCornerFor(corner, params, origin);
    uint sides = uint(params.w + 0.5);
    uint outgoing = corner;
    uint incoming = (corner + 3u) % 4u;
    bool afterExposed = (sides & (1u << outgoing)) != 0u;
    bool beforeExposed = (sides & (1u << incoming)) != 0u;
    bool cornerActive = afterExposed || beforeExposed || (corners & (1u << corner)) != 0u;
    GroundRimCorner after = groundRimCornerFor((corner + 1u) % 4u, params, origin);
    GroundRimCorner before = groundRimCornerFor((corner + 3u) % 4u, params, origin);
    float afterDrop = afterExposed
        ? shape.drop + shape.width * (after.drop - shape.drop) : 0.0;
    float beforeDrop = beforeExposed
        ? shape.drop + shape.width * (before.drop - shape.drop) : 0.0;
    vec2 offset = groundRimCornerOffsets[corner];
    vec2 along = groundRimCornerOffsets[(corner + 1u) % 4u] - offset;
    vec2 across = groundRimCornerOffsets[(corner + 3u) % 4u] - offset;
    GroundRimQuad result;
    result.vertices[0] = vec3(offset, 1.0 - (cornerActive ? shape.drop : 0.0));
    result.vertices[1] = vec3(offset + along * shape.width, 1.0 - afterDrop);
    result.vertices[2] = vec3(offset + (along + across) * shape.width, 1.0);
    result.vertices[3] = vec3(offset + across * shape.width, 1.0 - beforeDrop);
    return result;
}

bool groundRimCornerDiagonal(uint corner, vec4 params)
{
    uint sides = uint(params.w + 0.5);
    return (sides & ((1u << corner) | (1u << ((corner + 3u) % 4u)))) != 0u;
}

struct GroundRimSide
{
    vec4 along;
    vec4 width;
    vec4 drop;
    uint hash;
};

GroundRimSide groundRimSideFor(uint side, vec4 params, vec2 origin)
{
    uint start = groundRimSideCorners[side].x;
    uint end = groundRimSideCorners[side].y;
    GroundRimCorner a = groundRimCornerFor(start, params, origin);
    GroundRimCorner b = groundRimCornerFor(end, params, origin);
    vec2 world = origin + groundRimCornerOffsets[start];
    uint salt = side % 2u == 0u ? 101u : 211u;
    GroundRimSide result;
    result.along = vec4(
        a.width,
        a.width + (0.5 - a.width) * (0.40 + 0.35 * groundRimFractureUnit(world, salt)),
        0.5 + (0.5 - b.width) * (0.25 + 0.35 * groundRimFractureUnit(world, salt + 1u)),
        1.0 - b.width);
    result.width = vec4(a.width, 0.0, 0.0, b.width);
    result.drop = vec4(0.0);
    result.hash = groundRimFractureHash(world, salt + 4u);
    for (uint index = 1u; index <= 2u; ++index) {
        result.width[index] = min(min(0.40,
            params.x * (0.45 + 1.15 * groundRimFractureUnit(world, salt + index + 1u))),
            0.85 * min(result.along[index], 1.0 - result.along[index]));
    }
    if ((uint(params.w + 0.5) & (1u << side)) != 0u) {
        for (uint index = 0u; index < 4u; ++index) {
            result.drop[index] = a.drop + result.along[index] * (b.drop - a.drop);
        }
    }
    return result;
}

vec3 groundRimSideVertex(uint side, float along, float distance, float drop)
{
    vec2 xy = side == 0u ? vec2(along, distance)
        : side == 1u ? vec2(1.0 - distance, along)
        : side == 2u ? vec2(along, 1.0 - distance)
        : vec2(distance, along);
    return vec3(xy, 1.0 - drop);
}

GroundRimQuad groundRimSideQuad(uint side, uint segment, GroundRimSide shape)
{
    GroundRimQuad result;
    result.vertices[0] = groundRimSideVertex(side,
        shape.along[segment], 0.0, shape.drop[segment]);
    result.vertices[1] = groundRimSideVertex(side,
        shape.along[segment + 1u], 0.0, shape.drop[segment + 1u]);
    result.vertices[2] = groundRimSideVertex(side,
        shape.along[segment + 1u], shape.width[segment + 1u], 0.0);
    result.vertices[3] = groundRimSideVertex(side,
        shape.along[segment], shape.width[segment], 0.0);
    return result;
}

bool groundRimSampleTriangle(vec2 point, vec3 a, vec3 b, vec3 c,
    inout GroundRimSample result)
{
    vec2 ab = b.xy - a.xy;
    vec2 ac = c.xy - a.xy;
    vec2 ap = point - a.xy;
    float determinant = ab.x * ac.y - ab.y * ac.x;
    float u = (ap.x * ac.y - ap.y * ac.x) / determinant;
    float v = (ab.x * ap.y - ab.y * ap.x) / determinant;
    if (u < -0.000001 || v < -0.000001 || u + v > 1.000001) return false;
    float dzB = b.z - a.z;
    float dzC = c.z - a.z;
    result = GroundRimSample(
        max(0.0, 1.0 - (a.z + u * dzB + v * dzC)),
        vec2(-(dzB * ac.y - dzC * ab.y) / determinant,
            -(ab.x * dzC - ac.x * dzB) / determinant));
    return true;
}

bool groundRimSampleQuad(vec2 point, GroundRimQuad quad, bool diagonal,
    inout GroundRimSample result)
{
    return diagonal
        ? groundRimSampleTriangle(point, quad.vertices[0], quad.vertices[1], quad.vertices[2], result) ||
            groundRimSampleTriangle(point, quad.vertices[0], quad.vertices[2], quad.vertices[3], result)
        : groundRimSampleTriangle(point, quad.vertices[0], quad.vertices[1], quad.vertices[3], result) ||
            groundRimSampleTriangle(point, quad.vertices[1], quad.vertices[2], quad.vertices[3], result);
}

bool groundRimProfileValid(vec4 params, vec2 origin)
{
    return !any(isnan(params)) && !any(isinf(params)) &&
        params.x > 0.0 && params.x <= 0.5 && params.y > 0.0 &&
        params.z > params.y * 1.35 && params.z <= 1.0 &&
        !any(isnan(origin)) && !any(isinf(origin));
}

GroundRimSample groundRimFinishSample(vec2 position, vec2 point, GroundRimSample result)
{
    if (point.x != position.x) result.gradient.x = 0.0;
    if (point.y != position.y) result.gradient.y = 0.0;
    return result;
}

GroundRimSample groundRimSample(vec2 position, vec4 params, uint corners, vec2 origin)
{
    GroundRimSample result = GroundRimSample(0.0, vec2(0.0));
    if (!groundRimProfileValid(params, origin) || any(isnan(position)) || any(isinf(position))) {
        return result;
    }
    vec2 point = clamp(position, vec2(0.0), vec2(1.0));
    for (uint corner = 0u; corner < 4u; ++corner) {
        float width = groundRimCornerFor(corner, params, origin).width;
        vec2 offset = groundRimCornerOffsets[corner];
        if (abs(point.x - offset.x) <= width && abs(point.y - offset.y) <= width &&
            groundRimSampleQuad(point, groundRimCornerQuad(corner, params, corners, origin),
                groundRimCornerDiagonal(corner, params), result)) {
            return groundRimFinishSample(position, point, result);
        }
    }
    uint sides = uint(params.w + 0.5);
    for (uint side = 0u; side < 4u; ++side) {
        if ((sides & (1u << side)) == 0u) continue;
        GroundRimSide shape = groundRimSideFor(side, params, origin);
        float along = side % 2u == 0u ? point.x : point.y;
        for (uint segment = 0u; segment < 3u; ++segment) {
            if (along < shape.along[segment] || along > shape.along[segment + 1u]) continue;
            if (groundRimSampleQuad(point, groundRimSideQuad(side, segment, shape),
                ((shape.hash >> segment) & 1u) != 0u, result)) {
                return groundRimFinishSample(position, point, result);
            }
        }
    }
    return GroundRimSample(0.0, vec2(0.0));
}

struct GroundRimDeformation
{
    vec3 position;
    vec3 jacobian;
};

GroundRimDeformation groundRimDeformation(vec3 position, vec4 params, uint corners, vec2 origin)
{
    GroundRimDeformation result = GroundRimDeformation(position, vec3(0.0,0.0,1.0));
    if (isnan(position.z) || isinf(position.z) || position.z <= 1.0 - params.z) {
        return result;
    }
    GroundRimSample rimSample = groundRimSample(position.xy, params, corners, origin);
    if (rimSample.drop <= 0.0) {
        return result;
    }
    float rawBand = (position.z - (1.0 - params.z)) / params.z;
    float band = clamp(rawBand, 0.0, 1.0);
    if (band <= 0.0) {
        return result;
    }
    float derivative = rawBand <= 1.0 ? 1.0 / params.z : 0.0;
    result.position.z -= band * rimSample.drop;
    result.jacobian = vec3(-band * rimSample.gradient, 1.0 - derivative * rimSample.drop);
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

#ifndef WATER_CELL_FEATURES_GLSL
#define WATER_CELL_FEATURES_GLSL

// Static features of one triangular-lattice cell. Keep generation shared by
// the compute cache and the exact procedural fallback in the water shader.
struct WaterCellFeatures {
    vec4 pointAndAxis;
    vec4 shape;
};

vec2 hash22(vec2 value)
{
    vec3 value3 = fract(vec3(value.xyx) * vec3(0.1031, 0.1030, 0.0973));
    value3 += dot(value3, value3.yxz + 33.33);
    return fract((value3.xx + value3.yz) * value3.zy);
}

WaterCellFeatures generateWaterCellFeatures(vec2 cell)
{
    vec2 latticePoint = vec2(cell.x + cell.y * 0.5, cell.y * 0.8660254);
    vec2 jitter = hash22(cell) - vec2(0.5);
    vec2 point = latticePoint + vec2(
        jitter.x * 0.68 + jitter.y * 0.12,
        jitter.y * 0.58 - jitter.x * 0.10);
    vec2 axis = hash22(cell + vec2(41.73, 23.19)) * 2.0 - vec2(1.0);
    axis *= inversesqrt(max(dot(axis, axis), 0.001));
    float aspect = mix(0.62, 1.48, hash22(cell + vec2(3.11, 57.29)).y);
    float weight = mix(0.72, 1.34, hash22(cell + vec2(19.17, 7.43)).x);
    return WaterCellFeatures(vec4(point, axis), vec4(aspect, weight, 0.0, 0.0));
}

#ifdef WATER_CELL_CACHE_WRITE
#define WATER_CELL_ACCESS
#else
#define WATER_CELL_ACCESS readonly
#endif
layout(std430, set = 0, binding = 16) WATER_CELL_ACCESS buffer WaterCellCache {
    ivec4 origins;
    uvec4 dimensions; // width, height, reserved, enabled
    WaterCellFeatures cells[];
} waterCellCache;

#ifndef WATER_CELL_CACHE_WRITE
// Specializing the diagnostic/hardware fallback removes buffer reads and
// bounds checks entirely, so its timing represents procedural generation.
layout(constant_id = 1) const bool useWaterCellCache = true;
WaterCellFeatures waterCellFeatures(vec2 cell, uint pattern)
{
    if (!useWaterCellCache) {
        return generateWaterCellFeatures(cell);
    }
    vec2 origin = vec2(pattern == 0u
        ? waterCellCache.origins.xy : waterCellCache.origins.zw);
    vec2 local = cell - origin;
    // Check floating coordinates before conversion, including for extreme
    // editor views. Uncached cells keep the original unbounded field.
    if (waterCellCache.dimensions.w != 0u &&
        all(greaterThanEqual(local, vec2(0.0))) &&
        all(lessThan(local, vec2(waterCellCache.dimensions.xy)))) {
        uvec2 index = uvec2(local);
        uint width = waterCellCache.dimensions.x;
        uint height = waterCellCache.dimensions.y;
        return waterCellCache.cells[(pattern * height + index.y) * width + index.x];
    }
    return generateWaterCellFeatures(cell);
}
#endif

#endif

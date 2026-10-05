#version 460
#extension GL_GOOGLE_include_directive : require
#define WATER_CELL_CACHE_WRITE
#include "WaterCellFeatures.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(push_constant) uniform CachePlan {
    ivec4 origins;
    uvec4 dimensions;
} plan;

void main()
{
    uvec3 index = gl_GlobalInvocationID;
    if (all(equal(index, uvec3(0u)))) {
        waterCellCache.origins = plan.origins;
        waterCellCache.dimensions = plan.dimensions;
    }
    if (index.x >= plan.dimensions.x || index.y >= plan.dimensions.y || index.z >= 2u) {
        return;
    }
    ivec2 origin = index.z == 0u ? plan.origins.xy : plan.origins.zw;
    vec2 cell = vec2(origin + ivec2(index.xy));
    uint offset = (index.z * plan.dimensions.y + index.y) * plan.dimensions.x + index.x;
    waterCellCache.cells[offset] = generateWaterCellFeatures(cell);
}

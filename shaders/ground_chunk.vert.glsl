#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inFaceCoord;
layout(location = 3) in vec4 inWallCoverage;
layout(location = 4) in uint inTileSlot;

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
layout(location = 6) out vec3 outWorldPosition;
layout(location = 7) flat out uint outDrawInstance;
layout(location = 11) flat out vec4 outWallCoverage;

#include "DrawInstance.glsl"
#include "SceneFrame.glsl"

// One dynamic material entry per logical tile. The indexed chunk contributes
// its tile slot; firstInstance selects this scene's contiguous material range.
#define draw drawInstances.instances[uint(gl_InstanceIndex) + inTileSlot]

void main()
{
    gl_Position = frame.clipFromWorld * vec4(inPosition, 1.0);
    outShadowPosition = frame.shadowFromWorld * vec4(inPosition, 1.0);
    outShadowPosition.z = clamp(outShadowPosition.z, 0.0, 1.0);
    outWorldPosition = inPosition;
    outNormal = inNormal;
    vec2 faceCoord = inFaceCoord * draw.materialOptions.yz;
    outFaceCoordU = faceCoord.x;
    outFaceCoordV = faceCoord.y;
    outDrawInstance = uint(gl_InstanceIndex) + inTileSlot;
    // Every vertex in a patch carries the same four weights. Interpolation
    // uses the original quad coordinates in the shared fragment shader.
    outWallCoverage = inWallCoverage;
}

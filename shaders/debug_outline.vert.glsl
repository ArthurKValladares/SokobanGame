#version 460
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 7) flat out uint outDrawInstance;
#include "SceneFrame.glsl"
#include "DrawInstance.glsl"
#define draw drawInstances.instances[gl_InstanceIndex]
#include "DebugOutline.glsl"
void main()
{
    outDrawInstance = uint(gl_InstanceIndex);
    outlinePosition(inPosition, inNormal);
}

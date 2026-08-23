#version 460

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
layout(location = 4) flat out uint outTextureIndex;
layout(location = 5) flat out uint outMaterialFlags;
layout(location = 6) out vec3 outWorldPosition;

struct PointLightData
{
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneLighting
{
    vec4 sunShadowRightAndHalfWidth;
    vec4 sunShadowUpAndHalfHeight;
    vec4 sunShadowForwardAndDepthRange;
    vec4 sunShadowCenterAndNearestDepth;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
} lighting;
layout(push_constant) uniform PushConstants
{
    vec4 vertices[4];
    vec4 shadowVertices[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
} pc;

const int indices[6] = int[6](0, 1, 2, 0, 2, 3);
const vec2 faceCoords[4] = vec2[4](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0));

vec3 worldFromSunShadow(vec4 shadowPosition)
{
    vec3 clip = shadowPosition.xyz / max(abs(shadowPosition.w), 0.0001);
    vec3 center = lighting.sunShadowCenterAndNearestDepth.xyz;
    vec3 forward = lighting.sunShadowForwardAndDepthRange.xyz;
    float worldForward = lighting.sunShadowCenterAndNearestDepth.w +
        clip.z * lighting.sunShadowForwardAndDepthRange.w;
    float relativeForward = worldForward - dot(center, forward);
    return center +
        lighting.sunShadowRightAndHalfWidth.xyz *
            (clip.x * lighting.sunShadowRightAndHalfWidth.w) +
        lighting.sunShadowUpAndHalfHeight.xyz *
            (clip.y * lighting.sunShadowUpAndHalfHeight.w) +
        forward * relativeForward;
}

void main()
{
    const int index = indices[gl_VertexIndex];
    gl_Position = pc.vertices[index];
    outShadowPosition = pc.shadowVertices[index];
    outWorldPosition = worldFromSunShadow(outShadowPosition);
    vec2 faceCoord = faceCoords[index] * pc.materialOptions.yz;
    outFaceCoordU = faceCoord.x;
    outFaceCoordV = faceCoord.y;
    outNormal = pc.normalAndAmbientRed.xyz;
    // Tile faces never use per-vertex texture indices (textureOptions.x is 0
    // on this path), but the shared fragment shader consumes location 4, so
    // the interface must still provide it.
    outTextureIndex = 0u;
    outMaterialFlags = 0u;
}

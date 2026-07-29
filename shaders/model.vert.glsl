#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in float inTextureIndex;

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 3) out vec3 outNormal;
layout(location = 4) out float outTextureIndex;

layout(push_constant) uniform PushConstants
{
    vec4 clipFromModel[4];
    vec4 shadowFromModel[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
} pc;

void main()
{
    mat4 clipTransform = mat4(
        pc.clipFromModel[0],
        pc.clipFromModel[1],
        pc.clipFromModel[2],
        pc.clipFromModel[3]);
    mat4 shadowTransform = mat4(
        pc.shadowFromModel[0],
        pc.shadowFromModel[1],
        pc.shadowFromModel[2],
        pc.shadowFromModel[3]);

    gl_Position = clipTransform * vec4(inPosition, 1.0);
    outShadowPosition = shadowTransform * vec4(inPosition, 1.0);
    outFaceCoordU = inUv.x;
    outFaceCoordV = inUv.y;
    outTextureIndex = inTextureIndex;
    vec3 normal = inNormal;
    // Standard models use gridColor.xyz for inverse scale and a negative W
    // as the marker. Mirror-energy models need gridColor for their effect and
    // retain unit normal scaling.
    if (pc.gridColor.w < 0.0) {
        normal *= pc.gridColor.xyz;
    }

    vec3 rotation = pc.normalAndAmbientRed.xyz;
    float cosine = cos(rotation.x);
    float sine = sin(rotation.x);
    normal = vec3(
        normal.x,
        cosine * normal.y - sine * normal.z,
        sine * normal.y + cosine * normal.z);
    cosine = cos(rotation.y);
    sine = sin(rotation.y);
    normal = vec3(
        cosine * normal.x + sine * normal.z,
        normal.y,
        -sine * normal.x + cosine * normal.z);
    cosine = cos(rotation.z);
    sine = sin(rotation.z);
    outNormal = normalize(vec3(
        cosine * normal.x - sine * normal.y,
        sine * normal.x + cosine * normal.y,
        normal.z));
}

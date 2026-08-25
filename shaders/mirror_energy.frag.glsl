#version 460

layout(set = 0, binding = 2) uniform sampler2D modelTextures[MODEL_TEXTURE_COUNT];

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 4) flat in uint inTextureIndex;
layout(location = 5) flat in uint inMaterialFlags;
layout(location = 6) in vec3 inWorldPosition;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 0) out vec4 outColor;

// One draw's parameters, read back by instance index. T1 moved these out of
// push constants so that consecutive draws sharing a pipeline can collapse
// into a single instanced draw.
struct DrawInstance
{
    vec4 vertices[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
};
layout(std430, set = 0, binding = 10) readonly buffer DrawInstances
{
    DrawInstance instances[];
} drawInstances;

#define draw drawInstances.instances[inDrawInstance]


struct PointLightData
{
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneFrame
{
    mat4 clipFromWorld;
    mat4 shadowFromWorld;
    vec4 cameraPositionAndNearPlane;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
} frame;

vec4 sampledMaterial()
{
    int materialMode = int(draw.textureOptions.x + 0.5);
    if (materialMode == 2 && inTextureIndex != 0u) {
        int textureIndex = clamp(int(inTextureIndex - 1u), 0, MODEL_TEXTURE_COUNT - 1);
        vec2 uv = vec2(inFaceCoordU, inFaceCoordV);
        if ((inMaterialFlags & 1u) != 0u) {
            uv.y = fract(uv.y + draw.materialOptions.y);
        }
        return texture(modelTextures[textureIndex], uv);
    }
    if (materialMode == 1) {
        int textureIndex = clamp(
            int(draw.materialOptions.z + 0.5),
            0,
            MODEL_TEXTURE_COUNT - 1);
        return texture(
            modelTextures[textureIndex],
            vec2(inFaceCoordU, inFaceCoordV));
    }
    return vec4(1.0);
}

void main()
{
    vec4 material = sampledMaterial();
    float textureInfluence = clamp(draw.shadowOptions.w, 0.0, 1.0);
    float luminance = dot(material.rgb, vec3(0.2126, 0.7152, 0.0722));
    float detail = mix(1.0, 0.48 + luminance * 0.72, textureInfluence);

    float pulse = 0.5 + 0.5 * sin(
        draw.materialOptions.w * draw.gridColor.z +
        gl_FragCoord.x * 0.018 +
        gl_FragCoord.y * 0.013);
    float pulseScale = mix(
        1.0 - draw.gridColor.w,
        1.0 + draw.gridColor.w,
        pulse);

    float scanWave = 0.5 + 0.5 * sin(
        gl_FragCoord.y * draw.shadowOptions.x -
        draw.materialOptions.w * draw.shadowOptions.y);
    float scanScale = mix(
        1.0 - draw.shadowOptions.z,
        1.0 + draw.shadowOptions.z,
        scanWave);

    float rim = 0.0;
    if (length(inNormal) > 0.0001) {
        // The rim term is sign-independent - it uses abs(dot(...)) - so
        // this stayed plausible while it was a compiled-in isometric
        // constant. It is the real view direction now.
        vec3 viewDirection = normalize(
            frame.cameraPositionAndNearPlane.xyz - inWorldPosition);
        rim = pow(
            1.0 - abs(dot(normalize(inNormal), viewDirection)),
            max(draw.gridColor.x, 0.01)) * draw.gridColor.y;
    }

    vec3 normalizedTexture = material.rgb /
        max(max(material.r, material.g), max(material.b, 0.15));
    vec3 textureTint = mix(
        vec3(1.0),
        normalizedTexture,
        textureInfluence * 0.55);
    vec3 energyColor =
        draw.color.rgb * textureTint *
        (detail * pulseScale * scanScale + rim);
    float alpha = draw.color.a * material.a *
        clamp(0.78 + rim * 0.18 + pulse * 0.12, 0.0, 1.25);
    outColor = vec4(energyColor, alpha);
}

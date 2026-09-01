#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform sampler2D modelTextures[];

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 6) in vec3 inWorldPosition;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 9) in vec2 inUv1;
// Relative to the owning model; draw.passData[0].x makes it absolute.
layout(location = 10) flat in uint inMaterialIndex;
layout(location = 0) out vec4 outColor;

#include "DrawInstance.glsl"

#define draw drawInstances.instances[inDrawInstance]

#include "Material.glsl"

#include "DrawMode.glsl"

#include "SceneFrame.glsl"

Material modelMaterial()
{
    // Entry zero is the reserved published-nothing fallback; see the standard
    // model shader for why the relative index is dropped for a zero base.
    int materialBase = int(draw.passData[0].x + 0.5);
    return materials.entries[
        materialBase == 0 ? 0 : materialBase + int(inMaterialIndex)];
}

vec3 sampledMaterialRgb(Material material)
{
    int materialMode = int(draw.textureOptions.x + 0.5);
    vec3 result = material.baseColorFactor.rgb;
    if (materialMode == DRAW_MODE_GLTF_MATERIAL) {
        int materialTexture = int(material.primaryTextureHandles.x);
        // Mirror energy's explicit material subset is base-colour RGB, its UV
        // selection, and scrolling. Factor/texture alpha, alpha mode/cutoff,
        // and every PBR lighting map are deliberately ignored: this effect
        // owns its opacity and lighting response.
        if (materialTexture == 0) {
            return result;
        }
        int textureIndex = max(materialTexture - 1, 0);
        vec2 uv = material.textureUvSets.x == 1u
            ? inUv1
            : vec2(inFaceCoordU, inFaceCoordV);
        if ((material.materialState.z & 1u) != 0u) {
            uv.y = fract(uv.y + draw.materialOptions.y);
        }
        return result * texture(
            modelTextures[nonuniformEXT(textureIndex)], uv).rgb;
    }
    if (materialMode == DRAW_MODE_MANIFEST_TEXTURE) {
        int textureIndex = max(int(draw.materialOptions.z + 0.5), 0);
        return result * texture(
            modelTextures[nonuniformEXT(textureIndex)],
            vec2(inFaceCoordU, inFaceCoordV)).rgb;
    }
    return result;
}

void main()
{
    Material materialState = modelMaterial();
    if (draw.passData[0].y > 0.5 && !gl_FrontFacing &&
        materialState.materialState.w == 0u) {
        discard;
    }
    vec3 material = sampledMaterialRgb(materialState);
    float textureInfluence = clamp(draw.shadowOptions.w, 0.0, 1.0);
    float luminance = dot(material, vec3(0.2126, 0.7152, 0.0722));
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

    vec3 normalizedTexture = material /
        max(max(material.r, material.g), max(material.b, 0.15));
    vec3 textureTint = mix(
        vec3(1.0),
        normalizedTexture,
        textureInfluence * 0.55);
    vec3 energyColor =
        draw.color.rgb * textureTint *
        (detail * pulseScale * scanScale + rim);
    float alpha = draw.color.a *
        clamp(0.78 + rim * 0.18 + pulse * 0.12, 0.0, 1.25);
    outColor = vec4(energyColor, alpha);
}

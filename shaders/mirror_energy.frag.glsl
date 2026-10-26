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

float energyHash(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float energyNoise(vec3 p)
{
    vec3 cell = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(energyHash(cell), energyHash(cell + vec3(1, 0, 0)), f.x),
            mix(energyHash(cell + vec3(0, 1, 0)),
                energyHash(cell + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(energyHash(cell + vec3(0, 0, 1)),
                energyHash(cell + vec3(1, 0, 1)), f.x),
            mix(energyHash(cell + vec3(0, 1, 1)),
                energyHash(cell + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float energyCloud(vec3 p)
{
    return energyNoise(p) * 0.62 +
        energyNoise(p * 2.03 + vec3(7.1, 3.7, 1.3)) * 0.26 +
        energyNoise(p * 4.09 + vec3(1.7, 9.2, 5.4)) * 0.12;
}

float softStrand(float distanceToStrand, float width, float pixelWidth)
{
    float radius = max(width, pixelWidth * 0.75);
    float d = distanceToStrand / radius;
    // Preserve coverage as a strand becomes smaller than a pixel.
    return exp(-d * d) * min(width / radius, 1.0);
}

vec4 mirrorBeam()
{
    // Crossed, uncapped ribbons share U along the complete light path and V
    // across it. Physical distance keeps the wisp size stable on long legs.
    float beamLength = max(draw.passData[1].x, 0.0001);
    float along = inFaceCoordU * beamLength;
    float across = inFaceCoordV * 2.0 - 1.0;
    float time = draw.materialOptions.w * draw.passData[1].z;
    float phase = dot(draw.vertices[0].xyz + draw.vertices[1].xyz,
        vec3(1.71, 2.37, 0.73));
    float halo = smoothstep(0.5, 0.9, draw.passData[1].y);
    float pixelWidth = max(fwidth(across), 0.001);
    float edge = pow(max(1.0 - across * across, 0.0), 2.0);
    float endFade = smoothstep(0.0, min(0.14, beamLength * 0.2),
        min(along, beamLength - along));

    float bend = sin(along * 2.7 - time * 1.4 + phase) *
        mix(0.11, 0.48, halo) +
        sin(along * 5.3 + time * 0.8 + phase * 1.7) *
        mix(0.04, 0.13, halo);
    float curl = sin(along * 3.8 - time * 1.05 + phase + 2.4) * 0.52;
    float cloud = energyCloud(vec3(along * 1.8 - time * 0.7,
        across * 1.6, phase * 0.2 + time * 0.17));
    float flow = 0.5 + 0.5 * sin(along * 6.2 - time * 4.0 +
        cloud * 5.0 + phase);
    float spine = softStrand(across - bend, mix(0.19, 0.045, halo), pixelWidth);
    float filament = softStrand(across - curl, 0.038, pixelWidth) * halo;
    float wisp = softStrand(across - bend, 0.26, pixelWidth) * halo;
    float haze = edge * (0.45 + cloud * 0.55);
    float energy = spine * (0.52 + flow * 0.48) +
        filament * (0.3 + cloud * 0.7);
    float alpha = draw.color.a * endFade * edge *
        (haze * mix(0.20, 0.16, halo) +
            wisp * cloud * 0.65 + energy * 0.92);
    vec3 tint = mix(draw.color.rgb, vec3(0.78, 0.72, 1.0),
        halo * (1.0 - cloud) * 0.24);
    vec3 color = tint * (1.25 + flow * 0.45) +
        vec3(0.72, 0.9, 1.0) * energy * 0.65;
    return vec4(color, clamp(alpha, 0.0, 1.0));
}

void main()
{
    // Positive selects beam ribbons, negative preserves gate shutters, and
    // ghost models keep zero. Linked auras have their separate selector.
    if (draw.passData[0].w > 0.5) {
        outColor = mirrorBeam();
        if (outColor.a < 0.0015) {
            discard;
        }
        return;
    }

    Material materialState = modelMaterial();
    if (draw.passData[0].y > 0.5 && !gl_FrontFacing &&
        materialState.materialState.w == 0u) {
        discard;
    }
    vec3 material = sampledMaterialRgb(materialState);
    float textureInfluence = clamp(draw.shadowOptions.w, 0.0, 1.0);
    float luminance = dot(material, vec3(0.2126, 0.7152, 0.0722));
    float detail = mix(1.0, 0.48 + luminance * 0.72, textureInfluence);

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
    bool linkedAura = draw.passData[0].z > 0.5;
    if (linkedAura) {
        // Two differently-oriented, world-anchored waves keep the haze from
        // reading as a scrolling texture. Their interference produces wisps
        // that curl over the enlarged copy of the linked object's own mesh.
        float time = draw.materialOptions.w * draw.gridColor.z;
        float risingWisp = sin(
            (inWorldPosition.x + inWorldPosition.y) * 7.3 - time +
            sin(inWorldPosition.z * 10.7 + time * 0.61));
        float crossWisp = sin(
            (inWorldPosition.x - inWorldPosition.y) * 11.1 + time * 0.73 +
            sin(inWorldPosition.z * 6.4 - time * 0.47));
        float wisp = smoothstep(
            -0.72,
            0.86,
            risingWisp * 0.68 + crossWisp * 0.32);
        float breathing = 0.5 + 0.5 * sin(
            time * 0.52 + inWorldPosition.z * 5.1);
        float wispStrength = clamp(draw.gridColor.w, 0.0, 0.9);
        float haze = mix(1.0 - wispStrength, 1.0 + wispStrength, wisp);
        vec3 auraColor = draw.color.rgb * textureTint *
            (detail * (0.48 + haze * 0.42) + rim * 0.72);
        float alpha = draw.color.a * clamp(
            0.30 + wisp * 0.42 + breathing * 0.12 + rim * 0.22,
            0.0,
            1.0);
        outColor = vec4(auraColor, alpha);
        return;
    }

    if (draw.passData[0].w < -0.5) {
        float pulse = 0.5 + 0.5 * sin(
            draw.materialOptions.w * draw.gridColor.z +
            gl_FragCoord.x * 0.018 + gl_FragCoord.y * 0.013);
        float scanWave = 0.5 + 0.5 * sin(
            gl_FragCoord.y * draw.shadowOptions.x -
            draw.materialOptions.w * draw.shadowOptions.y);
        float pulseScale = mix(1.0 - draw.gridColor.w,
            1.0 + draw.gridColor.w, pulse);
        float scanScale = mix(1.0 - draw.shadowOptions.z,
            1.0 + draw.shadowOptions.z, scanWave);
        outColor = vec4(draw.color.rgb * textureTint *
            (detail * pulseScale * scanScale + rim),
            draw.color.a * clamp(0.78 + rim * 0.18 + pulse * 0.12, 0.0, 1.25));
        return;
    }

    // Flow lives in model-relative world units, so moving the camera never
    // slides a screen-space scanline across the reflection. Two advecting
    // noise scales warp the rising light into irregular, connected wisps.
    vec3 p = inWorldPosition - draw.vertices[3].xyz;
    float time = draw.materialOptions.w;
    vec3 drift = vec3(time * 0.12, -time * 0.09, -time * 0.28);
    float cloud = energyCloud(p * 3.4 + drift);
    float fineCloud = energyNoise(p * 8.1 + drift * 1.7 + cloud * 2.2);
    float flow = p.z * draw.shadowOptions.x -
        time * draw.shadowOptions.y + cloud * 6.0 +
        sin(p.x * 4.1 - p.y * 3.7 + time * 0.6) * 0.75;
    float veins = pow(max(1.0 - abs(sin(flow)), 0.0), 3.0);
    float wisps = smoothstep(0.30, 0.78, cloud * 0.72 + fineCloud * 0.28);
    float pulse = 0.5 + 0.5 * sin(time * draw.gridColor.z + p.z * 2.4);
    float pulseScale = mix(1.0 - draw.gridColor.w,
        1.0 + draw.gridColor.w, pulse);
    float flowStrength = clamp(draw.shadowOptions.z, 0.0, 1.0);
    float body = mix(0.82, 0.48 + wisps * 0.72, flowStrength);
    vec3 flowingTint = mix(draw.color.rgb, vec3(0.86, 0.76, 1.0),
        wisps * 0.26);
    vec3 energyColor = flowingTint * textureTint * detail * body * pulseScale +
        mix(flowingTint, vec3(0.88, 0.97, 1.0), 0.65) *
            (veins * flowStrength * 0.72 + rim * 0.95);
    float alpha = draw.color.a * clamp(
        0.48 + wisps * 0.34 + veins * flowStrength * 0.32 + rim * 0.24,
        0.0, 1.15);
    outColor = vec4(energyColor, alpha);
}

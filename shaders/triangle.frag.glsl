#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 1, binding = 0) uniform sampler2D modelTextures[];
layout(set = 0, binding = 8) uniform samplerCubeArray pointShadowMaps;

layout(location = 0) in vec4 inShadowPosition;
layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 3) in vec3 inNormal;
layout(location = 6) in vec3 inWorldPosition;
layout(location = 7) flat in uint inDrawInstance;
// Not read yet. It arrives here so that the vertex format, the pipelines and
// the interface between the stages are all in place and provably unchanged
// before the lighting that uses it changes everything at once.
layout(location = 8) in vec4 inTangent;
layout(location = 9) in vec2 inUv1;
// Which of the owning model's materials this fragment belongs to, relative to
// the model. draw.passData[0].x makes it absolute.
layout(location = 10) flat in uint inMaterialIndex;
layout(location = 0) out vec4 outColor;

#include "DrawInstance.glsl"

#define draw drawInstances.instances[inDrawInstance]

#include "Material.glsl"

// Entry zero is reserved as the published-nothing fallback: an untextured
// white surface. A base of zero means the model has not published, and its
// materialIndex means nothing yet, so it is ignored rather than added - which
// is what makes the fallback read white instead of some other model's range.
Material modelMaterial()
{
    int base = int(draw.passData[0].x + 0.5);
    return materials.entries[base == 0 ? 0 : base + int(inMaterialIndex)];
}

vec2 materialTextureUv(uint uvSet, uint materialFlags)
{
    vec2 uv = uvSet == 1u
        ? inUv1
        : vec2(inFaceCoordU, inFaceCoordV);
    if ((materialFlags & 1u) != 0u) {
        uv.y = fract(uv.y + draw.materialOptions.y);
    }
    return uv;
}

vec3 mappedSurfaceNormal(Material material)
{
    vec3 geometricNormal = normalize(inNormal);
    vec3 result = geometricNormal;
    int normalTexture = int(material.primaryTextureHandles.y);
    if (normalTexture != 0) {
        // Interpolation and non-uniform model scale can leave the transformed
        // tangent slightly outside the surface. Rebuild an orthonormal frame
        // here, after interpolation, rather than trusting a vertex-only frame.
        vec3 projectedTangent = inTangent.xyz -
            geometricNormal * dot(geometricNormal, inTangent.xyz);
        if (dot(projectedTangent, projectedTangent) < 0.000001) {
            vec3 axis = abs(geometricNormal.z) < 0.9
                ? vec3(0.0, 0.0, 1.0)
                : vec3(1.0, 0.0, 0.0);
            projectedTangent = cross(axis, geometricNormal);
        }
        vec3 tangent = normalize(projectedTangent);
        float handedness = inTangent.w < 0.0 ? -1.0 : 1.0;
        vec3 bitangent = cross(geometricNormal, tangent) * handedness;

        int textureIndex = max(normalTexture - 1, 0);
        vec3 tangentNormal = texture(
            modelTextures[nonuniformEXT(textureIndex)],
            materialTextureUv(
                material.textureUvSets.y,
                material.materialState.z)).xyz * 2.0 - 1.0;
        // glTF normal scale affects the tangent-plane perturbation only. The
        // sampled Z remains the map's authored distance from the surface.
        tangentNormal.xy *= material.materialScalars.y;
        result = normalize(
            tangent * tangentNormal.x +
            bitangent * tangentNormal.y +
            geometricNormal * tangentNormal.z);
    }

    // glTF double-sided back faces reverse their final shading normal before
    // lighting. Flipping after the normal-map transform keeps neutral and
    // perturbed samples consistent with the same rule.
    if (material.materialState.w != 0u && !gl_FrontFacing) {
        result = -result;
    }
    return result;
}

#include "SceneFrame.glsl"

#include "DrawMode.glsl"

#include "AmbientMask.glsl"

vec3 gaussianBlurredScene(vec2 uv)
{
    const float weights[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
    vec2 viewportSize = vec2(textureSize(sceneColor, 0));
    vec2 texel = abs(draw.materialOptions.w) / viewportSize;
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float weight = weights[x + 2] * weights[y + 2];
            result += texture(sceneColor, uv + vec2(float(x), float(y)) * texel).rgb * weight;
            totalWeight += weight;
        }
    }

    return result / totalWeight;
}

float bayer8x8(ivec2 pixel)
{
    const float thresholds[64] = float[64](
         0.0, 48.0, 12.0, 60.0,  3.0, 51.0, 15.0, 63.0,
        32.0, 16.0, 44.0, 28.0, 35.0, 19.0, 47.0, 31.0,
         8.0, 56.0,  4.0, 52.0, 11.0, 59.0,  7.0, 55.0,
        40.0, 24.0, 36.0, 20.0, 43.0, 27.0, 39.0, 23.0,
         2.0, 50.0, 14.0, 62.0,  1.0, 49.0, 13.0, 61.0,
        34.0, 18.0, 46.0, 30.0, 33.0, 17.0, 45.0, 29.0,
        10.0, 58.0,  6.0, 54.0,  9.0, 57.0,  5.0, 53.0,
        42.0, 26.0, 38.0, 22.0, 41.0, 25.0, 37.0, 21.0);
    ivec2 wrapped = pixel & ivec2(7);
    return (thresholds[wrapped.y * 8 + wrapped.x] + 0.5) / 64.0;
}

void applyEditorPreviewDither()
{
    if (draw.materialOptions.w >= 0.0) {
        return;
    }

    const float pixelScale = 2.0;
    const float coverage = 0.56;
    ivec2 ditherPixel = ivec2(floor(gl_FragCoord.xy / pixelScale));
    if (bayer8x8(ditherPixel) >= coverage) {
        discard;
    }
}

float gridMask()
{
    if (draw.gridColor.a <= 0.0 || draw.shadowOptions.w <= 0.0 || draw.materialOptions.y <= 0.0 || draw.materialOptions.z <= 0.0) {
        return 0.0;
    }

    vec2 faceCoord = vec2(inFaceCoordU, inFaceCoordV);
    vec2 wrapped = fract(faceCoord);
    vec2 distanceToLine = min(wrapped, 1.0 - wrapped);
    vec2 coordPerPixel = max(fwidth(faceCoord), vec2(0.00001));
    vec2 halfWidth = coordPerPixel * draw.shadowOptions.w * 0.5;
    vec2 feather = coordPerPixel;
    vec2 line = 1.0 - smoothstep(halfWidth, halfWidth + feather, distanceToLine);
    return max(line.x, line.y) * draw.gridColor.a;
}

float shadowFactor(vec4 shadowPosition, float diffuse)
{
    if (draw.shadowOptions.x <= 0.5 || diffuse <= 0.0 || abs(shadowPosition.w) <= 0.0001) {
        return 1.0;
    }

    vec3 shadowCoord = shadowPosition.xyz / shadowPosition.w;
    vec2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (any(lessThan(shadowUv, vec2(0.0))) ||
        any(greaterThan(shadowUv, vec2(1.0))) ||
        shadowCoord.z < 0.0 ||
        shadowCoord.z > 1.0) {
        return 1.0;
    }

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadowedSamples = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float closestDepth = texture(shadowMap, shadowUv + vec2(float(x), float(y)) * texel).r;
            shadowedSamples += shadowCoord.z - draw.shadowOptions.z > closestDepth ? 1.0 : 0.0;
        }
    }

    float shadowAmount = shadowedSamples / 9.0;
    return 1.0 - shadowAmount * draw.shadowOptions.y;
}

#define POINT_SHADOW_TAPS 5
#include "PointShadow.glsl"

// Cook-Torrance GGX. F3c replaced a wrapped-diffuse Blinn-Phong whose gloss
// came from two scene-wide knobs, so every surface in a level was equally
// shiny no matter what its glTF said.
//
// The pi that belongs under the Lambert term is folded into the light instead
// of divided out of the surface, which is the same thing as scaling every
// authored light intensity by pi. It keeps the swap from darkening every
// existing level threefold, and the specular below carries the matching pi so
// the two stay in the proportion the physics puts them in.
const float pi = 3.14159265359;

float distributionGgx(float normalDotHalf, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(pi * denominator * denominator, 0.0001);
}

// Smith with the direct-lighting remap of k. Height-correlated would be a
// little more accurate and costs a divide this does not need to spend.
float geometrySmith(float normalDotView, float normalDotLight, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float view = normalDotView / (normalDotView * (1.0 - k) + k);
    float light = normalDotLight / (normalDotLight * (1.0 - k) + k);
    return view * light;
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    float f = clamp(1.0 - cosine, 0.0, 1.0);
    float fSquared = f * f;
    return f0 + (1.0 - f0) * (fSquared * fSquared * f);
}

// Kept apart so the ambient mask can weigh the diffuse half on its own:
// occlusion estimates ambient visibility and has no business scaling a direct
// reflection, which is the same contract F2b established.
struct Shaded
{
    vec3 diffuse;
    vec3 specular;
};

Shaded shadeLight(
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 radiance,
    vec3 diffuseAlbedo,
    vec3 f0,
    float roughness)
{
    Shaded result = Shaded(vec3(0.0), vec3(0.0));
    float normalDotLight = dot(normal, lightDirection);
    if (normalDotLight <= 0.0) {
        return result;
    }
    float normalDotView = max(dot(normal, viewDirection), 0.0001);
    vec3 halfVector = normalize(lightDirection + viewDirection);
    vec3 fresnel = fresnelSchlick(
        max(dot(halfVector, viewDirection), 0.0), f0);
    float distribution = distributionGgx(
        max(dot(normal, halfVector), 0.0), roughness);
    float geometry = geometrySmith(normalDotView, normalDotLight, roughness);

    vec3 incoming = radiance * normalDotLight;
    // The pi here is the one folded into the light above.
    result.specular = incoming * fresnel * distribution * geometry * pi /
        max(4.0 * normalDotView * normalDotLight, 0.0001);
    result.diffuse = incoming * (vec3(1.0) - fresnel) * diffuseAlbedo;
    return result;
}

void main()
{
    applyEditorPreviewDither();

    // Read for every draw, not just the primitive-materials one. A draw with
    // no model behind it has a zero base and lands on the reserved fallback:
    // white factor, no texture, metallic 0, roughness 1. So this is a no-op
    // for tiles and particles, and the lighting below needs no branch to
    // find a metallic and a roughness for them.
    Material material = modelMaterial();
    vec4 materialColor = draw.color;
    int materialMode = int(draw.textureOptions.x + 0.5);
    bool modelDraw = isModelDraw(draw.gridColor);
    if (modelDraw) {
        // A mixed glTF mesh may be submitted to both passes. The recorder
        // selects the pass's material subset without multiplying pipelines.
        int alphaSelection = int(draw.textureOptions.w + 0.5);
        bool blendMaterial = material.materialState.y == 2u;
        if ((alphaSelection == 1 && blendMaterial) ||
            (alphaSelection == 2 && !blendMaterial)) {
            discard;
        }
        // A model containing any double-sided material disables fixed-function
        // culling for the whole draw. Restore authored single-sided behavior
        // per primitive; double-sided back faces continue to the normal flip.
        if (draw.passData[0].y > 0.5 && !gl_FrontFacing &&
            material.materialState.w == 0u) {
            discard;
        }
    }

    vec4 baseColorSample = vec4(1.0);
    if (materialMode == DRAW_MODE_GLTF_MATERIAL) {
        int materialTexture = int(material.primaryTextureHandles.x);
        if (materialTexture != 0) {
            int textureIndex = max(materialTexture - 1, 0);
            // Set 1 is the second unwrap; the loader falls it back to set 0
            // on a mesh that has only one, so this never reads nothing.
            vec2 uv = materialTextureUv(
                material.textureUvSets.x, material.materialState.z);
            baseColorSample = texture(
                modelTextures[nonuniformEXT(textureIndex)], uv);
        }
    } else if (materialMode == DRAW_MODE_PROCEDURAL_TEXTURE) {
        // Procedural quads use a one-based texture handle because zero means
        // that no runtime texture was resolved.
        float selectedTexture = draw.textureOptions.y - 1.0;
        int textureIndex = max(int(selectedTexture + 0.5), 0);
        materialColor *= texture(
            modelTextures[nonuniformEXT(textureIndex)],
            vec2(inFaceCoordU, inFaceCoordV));
    } else if (materialMode == DRAW_MODE_MANIFEST_TEXTURE) {
        int textureIndex = max(int(draw.materialOptions.z + 0.5), 0);
        baseColorSample = texture(
            modelTextures[nonuniformEXT(textureIndex)],
            vec2(inFaceCoordU, inFaceCoordV));
    }
    if (modelDraw) {
        // glTF base-colour RGB always multiplies factor and the selected base
        // texture, including a manifest override. Authored alpha instead
        // controls coverage: OPAQUE ignores it, MASK compares it to the
        // cutoff, and BLEND contributes it to output opacity.
        materialColor.rgb *=
            material.baseColorFactor.rgb * baseColorSample.rgb;
        float authoredAlpha =
            material.baseColorFactor.a * baseColorSample.a;
        if (material.materialState.y == 1u &&
            authoredAlpha < material.materialScalars.w) {
            discard;
        }
        if (material.materialState.y == 2u) {
            materialColor.a *= authoredAlpha;
        }
    } else if (materialMode == DRAW_MODE_MANIFEST_TEXTURE ||
        materialMode == DRAW_MODE_GLTF_MATERIAL) {
        materialColor *= baseColorSample;
    }
    vec3 color = mix(materialColor.rgb, draw.gridColor.rgb, gridMask());
    // Stays zero for anything unlit - the grid overlay or the 2D board -
    // so occlusion cannot touch surfaces that have no ambient term to occlude.
    float ambientMask = 0.0;
    if (length(inNormal) > 0.0001) {
        vec3 normal = mappedSurfaceNormal(material);
        vec3 lightDirection = length(draw.sunDirectionAndAmbientGreen.xyz) > 0.0001
            ? normalize(draw.sunDirectionAndAmbientGreen.xyz)
            : vec3(0.0, 0.0, 1.0);
        float lambertDiffuse = max(dot(normal, lightDirection), 0.0);
        vec3 ambient = vec3(
            draw.normalAndAmbientRed.w,
            draw.sunDirectionAndAmbientGreen.w,
            draw.sunRadianceAndAmbientBlue.w);
        float shadow = shadowFactor(inShadowPosition, lambertDiffuse);
        float skyFill = smoothstep(-0.35, 1.0, normal.z);

        // The material's own gloss, where the whole level used to share one
        // exponent. Roughness has a floor because a perfect mirror is a
        // delta function no point light can hit.
        float metallic = material.emissiveAndMetallic.w;
        float roughness = material.materialScalars.x;
        int metallicRoughnessTexture =
            int(material.primaryTextureHandles.z);
        if (metallicRoughnessTexture != 0) {
            int textureIndex = max(metallicRoughnessTexture - 1, 0);
            // glTF metallic-roughness images are linear data: G carries
            // perceptual roughness and B carries metallic. The authored
            // scalar factors multiply those channels rather than being
            // replaced by them.
            vec2 metallicRoughnessSample = texture(
                modelTextures[nonuniformEXT(textureIndex)],
                materialTextureUv(
                    material.textureUvSets.z,
                    material.materialState.z)).gb;
            roughness *= metallicRoughnessSample.x;
            metallic *= metallicRoughnessSample.y;
        }
        metallic = clamp(metallic, 0.0, 1.0);
        // GGX cannot represent a perfect delta with this finite sampling
        // path, so preserve the established floor after applying the map.
        roughness = clamp(roughness, 0.045, 1.0);
        // Dielectrics reflect about 4% head-on; a metal reflects its own
        // colour and has no diffuse lobe at all.
        vec3 f0 = mix(vec3(0.04), color, metallic);
        vec3 diffuseAlbedo = color * (1.0 - metallic);
        // A scene-wide dial on the specular half, kept from the old model so
        // a level that wanted a flatter look still has the knob. Its
        // companion, specularPower, is gone: roughness is the exponent now.
        float specularStrength = max(draw.textureOptions.z, 0.0);

        vec3 diffuseLight = vec3(0.0);
        vec3 specularLight = vec3(0.0);
        // The real direction from the surface to the camera. This used
        // to be a compiled-in constant matching one fixed isometric camera,
        // so every specular highlight was correct only from that angle and
        // wrong everywhere else the camera could go.
        vec3 viewDirection = normalize(
            frame.cameraPositionAndNearPlane.xyz - inWorldPosition);
        int pointLightCount = clamp(int(frame.pointLightMeta.x + 0.5), 0, 8);
        for (int lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
            PointLightData pointLight = frame.pointLights[lightIndex];
            vec3 toLight = pointLight.positionAndRange.xyz - inWorldPosition;
            float distanceToLight = length(toLight);
            float range = max(pointLight.positionAndRange.w, 0.001);
            if (distanceToLight <= 0.0001 || distanceToLight >= range) {
                continue;
            }
            vec3 pointDirection = toLight / distanceToLight;
            float pointLambert = max(dot(normal, pointDirection), 0.0);
            if (pointLambert <= 0.0) {
                continue;
            }
            float normalizedDistance = distanceToLight / range;
            float rangeWindow = max(
                1.0 - normalizedDistance * normalizedDistance *
                    normalizedDistance * normalizedDistance,
                0.0);
            float attenuation = rangeWindow * rangeWindow /
                max(distanceToLight * distanceToLight, 0.04);
            float pointShadow = pointShadowFactor(
                lightIndex, -toLight, normal);
            vec3 radiance = pointLight.colorAndIntensity.rgb *
                pointLight.colorAndIntensity.w * attenuation * pointShadow;
            Shaded point = shadeLight(
                normal, viewDirection, pointDirection, radiance,
                diffuseAlbedo, f0, roughness);
            diffuseLight += point.diffuse;
            specularLight += point.specular;
        }
        Shaded sun = shadeLight(
            normal,
            viewDirection,
            lightDirection,
            draw.sunRadianceAndAmbientBlue.rgb * shadow,
            diffuseAlbedo,
            f0,
            roughness);
        diffuseLight += sun.diffuse;
        specularLight += sun.specular;

        // A hemispheric approximation, kept from the old model: an up-facing
        // surface sees more of the sky. Both halves are here - the f0 half is
        // what stops a metal from going black wherever no light reaches it
        // directly, standing in for the environment probe this engine does
        // not have yet.
        //
        // specularStrength deliberately does not reach it. That knob dials
        // the gloss a level wants; this is not gloss, it is the only ambient
        // a metal gets, and letting the slider reach zero would render a
        // metallic surface black.
        vec3 ambientTerm = ambient * (1.0 + skyFill * 0.35);
        vec3 ambientContribution = ambientTerm * (diffuseAlbedo + f0);

        // glTF material occlusion is linear data in R. Strength interpolates
        // from no occlusion at zero to the sampled value at one. Applying the
        // result here affects both diffuse and metallic ambient fill, but no
        // direct, specular or emissive light. A missing map is a white sample.
        int occlusionTexture =
            int(material.occlusionTextureAndPadding.x);
        if (occlusionTexture != 0) {
            int textureIndex = max(occlusionTexture - 1, 0);
            float sampledOcclusion = texture(
                modelTextures[nonuniformEXT(textureIndex)],
                materialTextureUv(
                    material.materialState.x,
                    material.materialState.z)).r;
            float materialOcclusion = mix(
                1.0,
                sampledOcclusion,
                clamp(material.materialScalars.z, 0.0, 1.0));
            ambientContribution *= materialOcclusion;
        }

        // The emissive image is uploaded through an sRGB Vulkan format, so
        // sampling returns linear RGB here. Its alpha is not part of glTF's
        // emissive definition. A missing map is equivalent to white and lets
        // the factor stand on its own; neither path is clamped, preserving
        // authored HDR emission.
        vec3 emissive = material.emissiveAndMetallic.rgb;
        int emissiveTexture = int(material.primaryTextureHandles.w);
        if (emissiveTexture != 0) {
            int textureIndex = max(emissiveTexture - 1, 0);
            emissive *= texture(
                modelTextures[nonuniformEXT(textureIndex)],
                materialTextureUv(
                    material.textureUvSets.w,
                    material.materialState.z)).rgb;
        }

        color = diffuseLight + ambientContribution +
            specularLight * specularStrength + emissive;

        // The share of this pixel's light that came from ambient, which is
        // what the SSAO composite scales by.
        //
        // Both halves of the ambient fill count: the f0 half is ambient by
        // construction, and leaving it out - which is what "specular is
        // excluded from both sides" would have meant here - made a metallic
        // surface report a smaller ambient share than it had, and a fully
        // metallic one report zero over zero and take no occlusion at all.
        // Material occlusion has already reduced the ambient contribution, so
        // both sides describe the radiance that actually leaves the surface.
        // Direct light, specular and emissive stay in the denominator only,
        // letting screen-space AO compose with the material map instead of
        // applying either form of occlusion to non-ambient light.
        ambientMask = clamp(
            dot(ambientContribution, luminanceWeights) /
                max(dot(color, luminanceWeights), 0.0001),
            0.0,
            1.0);
    }

    int editorHighlight = int(draw.textureOptions.y + 0.5);
    if (modelDraw && editorHighlight > 0) {
        vec3 highlightColor = editorHighlight == 2
            ? vec3(1.0, 0.48, 0.08)
            : vec3(0.08, 0.88, 1.0);
        float tintStrength = editorHighlight == 2 ? 0.48 : 0.38;
        float pulse = 0.5 + 0.5 * sin(draw.materialOptions.w * 3.5);

        // A narrow screen-space sweep gives the opaque tint some life without
        // changing the model's geometry, texture, depth, or silhouette.
        float sweepPhase = fract(
            (gl_FragCoord.x + gl_FragCoord.y) * 0.0025 -
            draw.materialOptions.w * 0.55);
        float glimmer = 1.0 - smoothstep(0.0, 0.075, abs(sweepPhase - 0.5));

        color = mix(color, highlightColor, tintStrength + pulse * 0.04);
        color += mix(vec3(1.0), highlightColor, 0.35) * glimmer * 0.32;
        outColor = vec4(color, writeAmbientMask ? ambientMask : 1.0);
        return;
    }

    if (draw.materialOptions.x > 0.5) {
        vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneColor, 0));
        vec3 blurred = gaussianBlurredScene(uv);
        outColor = vec4(
            mix(blurred, color, materialColor.a),
            writeAmbientMask ? ambientMask : 1.0);
        return;
    }

    outColor = vec4(
        color, writeAmbientMask ? ambientMask : materialColor.a);
}

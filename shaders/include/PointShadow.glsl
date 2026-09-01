#ifndef SOKOBAN_POINT_SHADOW_GLSL
#define SOKOBAN_POINT_SHADOW_GLSL

// Cube-map depth back to a world-space distance from the light.
//
// This is the only part of point shadowing the scene and ground shaders
// currently agree on character-for-character. Their pointShadowFactor bodies
// have diverged - the scene takes five taps, the ground takes one - so those
// are deliberately left in place rather than merged here, because reconciling
// them changes how the ground is lit and deserves its own change.
float pointShadowWorldDistance(
    float depth, float nearPlane, float farPlane)
{
    float denominator = farPlane - depth * (farPlane - nearPlane);
    return denominator > 0.000001
        ? farPlane * nearPlane / denominator
        : farPlane;
}

#endif

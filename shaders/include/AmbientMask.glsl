#ifndef SOKOBAN_AMBIENT_MASK_GLSL
#define SOKOBAN_AMBIENT_MASK_GLSL

// The ambient-mask contract, shared by every opaque scene pipeline.
//
// Set on the opaque pipelines only. Screen-space occlusion estimates *ambient*
// visibility, so multiplying the finished pixel by it darkens direct sunlight
// as well, which no amount of occlusion should. When this is set the alpha
// channel stops carrying the material's alpha and carries the share of this
// pixel's light that came from the ambient term instead, and the composite
// scales its effect by it.
//
// Only the opaque pipelines can do this: a blended draw needs alpha to mean
// alpha. Those pipelines are created with the alpha channel masked out of
// their colour writes, so a translucent surface inherits the mask of whatever
// opaque geometry it sits in front of, which is the right answer anyway.
//
// Every shader that writes the mask must compute the same semantic ratio.
// Sharing the declaration is what keeps the two sides of that contract from
// drifting apart silently.
layout(constant_id = 0) const bool writeAmbientMask = false;

const vec3 luminanceWeights = vec3(0.2126, 0.7152, 0.0722);

#endif

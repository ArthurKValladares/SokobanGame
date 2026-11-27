#ifndef SOKOBAN_WIND_SWAY_GLSL
#define SOKOBAN_WIND_SWAY_GLSL

// The wind in grass and foliage. Requires SceneFrame.glsl; windLanes in
// SceneDrawLanes.hpp packs the two frame lanes read here.
//
// weight is a model's _WIND attribute: zero at a root, which stays where it
// is, up to one at a tip, which moves the most. It reaches the vertex shader
// negated, in the channel _GROUND_DEBRIS uses for its seeds, since no vertex
// is both a floating rock and a blade of grass.
//
// The offset is in world space and is added after the model's transform, so
// a decoration leans downwind whichever way it was turned. Gusts are a wave
// that rolls downwind across the whole board, bent a little from side to
// side so that it never reads as a straight front. Between gusts the grass
// stands nearly upright, and a quicker, smaller flutter keeps neighbours
// from moving as one. Shadows are drawn from the rest pose, and the caller
// looks the sun's shadow up from the rest pose too.
vec3 windSwayOffset(float weight, vec3 worldPosition)
{
    float amplitude = frame.windSway.x;
    if (weight <= 0.0 || amplitude <= 0.0) {
        return vec3(0.0);
    }
    vec2 wind = frame.windDirectionAndTime.xy;
    vec2 across = vec2(-wind.y, wind.x);
    float time = frame.windDirectionAndTime.z;
    float along = dot(worldPosition.xy, wind);
    float side = dot(worldPosition.xy, across);

    float bentFront = 1.7 * sin(side * frame.windSway.y * 0.6 + time * 0.23);
    float gust = 0.5 + 0.5 * sin(
        along * frame.windSway.y - time * frame.windSway.z + bentFront);
    float flutter = sin(
        time * frame.windSway.z * 3.1 + along * 5.3 + side * 7.1);

    // A blade bends more the farther up it goes.
    float bend = weight * weight;
    vec2 lean = (wind * (0.3 + 0.7 * gust) + across * (frame.windSway.w * flutter)) *
        (amplitude * bend);
    // What leans over also comes down a little, or it would look stretched.
    float drop = min(dot(lean, lean) * 2.0, 0.5 * length(lean));
    return vec3(lean, -drop);
}

#endif

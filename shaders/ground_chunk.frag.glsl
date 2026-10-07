#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 11) flat in vec4 inWallCoverage;
#define GROUND_RIM_WALL_WEIGHTS inWallCoverage
#include "GroundSplatFragment.glsl"

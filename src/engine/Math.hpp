#pragma once

namespace sokoban {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct GridPosition {
    int x = 0;
    int y = 0;
};

inline bool operator==(GridPosition left, GridPosition right)
{
    return left.x == right.x && left.y == right.y;
}

} // namespace sokoban

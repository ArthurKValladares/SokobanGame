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

struct GridPosition3 {
    int x = 0;
    int y = 0;
    int z = 0;
};

inline bool operator==(GridPosition left, GridPosition right)
{
    return left.x == right.x && left.y == right.y;
}

inline bool operator==(GridPosition3 left, GridPosition3 right)
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] inline GridPosition xy(GridPosition3 position)
{
    return { position.x, position.y };
}

} // namespace sokoban

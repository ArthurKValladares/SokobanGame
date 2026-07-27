#include "engine/SplatCanvas.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {
namespace {

// Fraction of the brush radius covered per stamp when dragging. Small enough
// that consecutive stamps overlap heavily, so a drag reads as one smooth
// stroke instead of a row of discs, but not so small that a long drag costs
// hundreds of stamps.
constexpr float strokeSpacingFraction = 0.25f;

// A drag across a large board could otherwise request an unbounded number of
// stamps in one frame (for example if the pointer jumps after an alt-tab).
constexpr int maxStampsPerSegment = 512;

[[nodiscard]] float brushFalloff(float distance, float radius, float hardness)
{
    if (radius <= 0.0f) {
        return 0.0f;
    }
    const float normalized = distance / radius;
    if (normalized >= 1.0f) {
        return 0.0f;
    }
    // hardness 1 -> flat disc, hardness 0 -> smooth falloff from the centre.
    const float solid = std::clamp(hardness, 0.0f, 1.0f);
    if (normalized <= solid) {
        return 1.0f;
    }
    const float feather = 1.0f - solid;
    if (feather <= 0.0f) {
        return 1.0f;
    }
    const float t = (normalized - solid) / feather;
    // Smoothstep, so the edge has no visible banding where it meets the
    // untouched map.
    return 1.0f - (t * t * (3.0f - 2.0f * t));
}

} // namespace

SplatCanvas SplatCanvas::createForBoard(
    uint32_t boardTilesWide,
    uint32_t boardTilesHigh,
    uint8_t initialWeight)
{
    SplatCanvas canvas;
    canvas.width_ = boardTilesWide * texelsPerTile;
    canvas.height_ = boardTilesHigh * texelsPerTile;
    canvas.weights_.assign(
        static_cast<std::size_t>(canvas.width_) * canvas.height_,
        initialWeight);
    return canvas;
}

std::optional<SplatCanvas> SplatCanvas::fromImage(const ImageData& image)
{
    constexpr std::size_t channels = 4;
    const std::size_t texels =
        static_cast<std::size_t>(image.width) * image.height;
    if (image.width == 0 || image.height == 0 ||
        image.rgba.size() < texels * channels) {
        return std::nullopt;
    }

    SplatCanvas canvas;
    canvas.width_ = image.width;
    canvas.height_ = image.height;
    canvas.weights_.resize(texels);
    for (std::size_t i = 0; i < texels; ++i) {
        canvas.weights_[i] =
            static_cast<uint8_t>(image.rgba[i * channels]);
    }
    return canvas;
}

Vec2 SplatCanvas::boardTiles() const
{
    return {
        static_cast<float>(width_) / static_cast<float>(texelsPerTile),
        static_cast<float>(height_) / static_cast<float>(texelsPerTile),
    };
}

uint8_t SplatCanvas::weightAt(uint32_t x, uint32_t y) const
{
    if (x >= width_ || y >= height_) {
        return 0;
    }
    return weights_[static_cast<std::size_t>(y) * width_ + x];
}

ImageData SplatCanvas::toImage() const
{
    ImageData image;
    image.width = width_;
    image.height = height_;
    image.rgba.resize(weights_.size() * 4);
    for (std::size_t i = 0; i < weights_.size(); ++i) {
        const std::byte value = static_cast<std::byte>(weights_[i]);
        image.rgba[i * 4 + 0] = value;
        image.rgba[i * 4 + 1] = value;
        image.rgba[i * 4 + 2] = value;
        image.rgba[i * 4 + 3] = static_cast<std::byte>(255);
    }
    return image;
}

bool SplatCanvas::stamp(Vec2 centerTiles, const Brush& brush)
{
    if (empty() || brush.radiusTiles <= 0.0f || brush.opacity <= 0.0f) {
        return false;
    }

    const float scale = static_cast<float>(texelsPerTile);
    const float centerX = centerTiles.x * scale;
    const float centerY = centerTiles.y * scale;
    const float radius = brush.radiusTiles * scale;

    // Only the texels the brush can reach are visited; a small brush on a
    // large board must not cost a full-canvas pass per stamp.
    const int minX = std::max(
        0, static_cast<int>(std::floor(centerX - radius)));
    const int maxX = std::min(
        static_cast<int>(width_) - 1,
        static_cast<int>(std::ceil(centerX + radius)));
    const int minY = std::max(
        0, static_cast<int>(std::floor(centerY - radius)));
    const int maxY = std::min(
        static_cast<int>(height_) - 1,
        static_cast<int>(std::ceil(centerY + radius)));
    if (minX > maxX || minY > maxY) {
        return false;
    }

    const float target =
        brush.color == BrushColor::White ? 255.0f : 0.0f;
    const float opacity = std::clamp(brush.opacity, 0.0f, 1.0f);
    bool changed = false;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            // Sample at the texel centre so the stamp is symmetric about the
            // pointer rather than biased half a texel up and left.
            const float dx = (static_cast<float>(x) + 0.5f) - centerX;
            const float dy = (static_cast<float>(y) + 0.5f) - centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float falloff =
                brushFalloff(distance, radius, brush.hardness);
            if (falloff <= 0.0f) {
                continue;
            }

            const std::size_t index =
                static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x);
            const float current = static_cast<float>(weights_[index]);
            const float blended =
                current + (target - current) * falloff * opacity;
            const auto updated = static_cast<uint8_t>(
                std::clamp(std::lround(blended), 0L, 255L));
            if (updated != weights_[index]) {
                weights_[index] = updated;
                changed = true;
            }
        }
    }
    return changed;
}

bool SplatCanvas::stampLine(Vec2 fromTiles, Vec2 toTiles, const Brush& brush)
{
    if (empty() || brush.radiusTiles <= 0.0f) {
        return false;
    }

    const float dx = toTiles.x - fromTiles.x;
    const float dy = toTiles.y - fromTiles.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float spacing =
        std::max(brush.radiusTiles * strokeSpacingFraction, 0.001f);
    const int steps = std::min(
        static_cast<int>(std::floor(length / spacing)), maxStampsPerSegment);

    // Always stamp the endpoint: a click without movement, and the exact tip
    // of a drag, both have to land.
    bool changed = false;
    for (int step = 1; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps + 1);
        changed |= stamp({ fromTiles.x + dx * t, fromTiles.y + dy * t }, brush);
    }
    changed |= stamp(toTiles, brush);
    return changed;
}

bool SplatCanvas::restore(const std::vector<uint8_t>& snapshot)
{
    if (snapshot.size() != weights_.size()) {
        return false;
    }
    weights_ = snapshot;
    return true;
}

} // namespace sokoban

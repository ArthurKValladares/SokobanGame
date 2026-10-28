#include "engine/SplatCanvas.hpp"

#include <algorithm>
#include <array>
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

// Proportional integer allocation with largest remainders keeps every texel
// exactly normalized while preserving the other layers to one byte of error.
void distributeWeights(const std::array<uint32_t, 4>& source,
    uint32_t total, uint32_t budget, uint8_t* destination)
{
    std::array<uint32_t, 4> remainders {};
    uint32_t used = 0;
    for (std::size_t layer = 0; layer < source.size(); ++layer) {
        destination[layer] = total == 0 ? 0 :
            static_cast<uint8_t>(source[layer] * budget / total);
        remainders[layer] = total == 0 ? 0 : source[layer] * budget % total;
        used += destination[layer];
    }
    while (used < budget) {
        const auto largest = std::max_element(remainders.begin(), remainders.end());
        const auto layer = static_cast<std::size_t>(largest - remainders.begin());
        ++destination[layer];
        *largest = 0;
        ++used;
    }
}

// Brush profile at `distance` texels from the centre, in 0..1.
//
// hardness 1 is a flat disc, hardness 0 feathers all the way from the centre.
// The outermost texel always feathers a little regardless, so a hard brush has
// a smooth boundary instead of a stair-stepped one.
[[nodiscard]] float brushFalloff(float distance, float radius, float hardness)
{
    if (radius <= 0.0f || distance >= radius) {
        return 0.0f;
    }
    const float rim = std::min(1.0f, radius * 0.5f);
    const float solid = std::clamp(hardness, 0.0f, 1.0f) * radius;
    const float featherStart = std::min(solid, radius - rim);
    if (distance <= featherStart) {
        return 1.0f;
    }
    const float feather = radius - featherStart;
    if (feather <= 0.0f) {
        return 1.0f;
    }
    const float t = (distance - featherStart) / feather;
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
        static_cast<std::size_t>(canvas.width_) * canvas.height_ * layerCount, 0);
    for (std::size_t i = 0; i < canvas.weights_.size(); i += layerCount) {
        canvas.weights_[i] = static_cast<uint8_t>(255 - initialWeight);
        canvas.weights_[i + 1] = initialWeight;
    }
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
    canvas.weights_.resize(texels * channels);
    for (std::size_t i = 0; i < texels; ++i) {
        uint8_t* destination = canvas.weights_.data() + i * channels;
        if (image.sourceChannels <= 2) {
            const auto gray = static_cast<uint8_t>(image.rgba[i * channels]);
            destination[0] = static_cast<uint8_t>(255 - gray);
            destination[1] = gray;
            destination[2] = destination[3] = 0;
        } else {
            std::array<uint32_t, 4> source {};
            uint32_t total = 0;
            for (std::size_t layer = 0; layer < channels; ++layer) {
                // RGB files have no authored layer-3 weight. stb_image's
                // synthesized opaque alpha is not part of the paint mask.
                source[layer] = image.sourceChannels == 3 && layer == 3 ? 0 :
                    static_cast<uint8_t>(image.rgba[i * channels + layer]);
                total += source[layer];
            }
            if (total == 0) {
                destination[0] = 255;
                destination[1] = destination[2] = destination[3] = 0;
            } else {
                distributeWeights(source, total, 255, destination);
            }
        }
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
    return weightAt(x, y, 1);
}

uint8_t SplatCanvas::weightAt(uint32_t x, uint32_t y, uint32_t layer) const
{
    if (x >= width_ || y >= height_ || layer >= layerCount) {
        return 0;
    }
    return weights_[(static_cast<std::size_t>(y) * width_ + x) * layerCount + layer];
}

ImageData SplatCanvas::toImage() const
{
    ImageData image;
    image.width = width_;
    image.height = height_;
    image.rgba.resize(weights_.size());
    for (std::size_t i = 0; i < weights_.size(); ++i) {
        image.rgba[i] = static_cast<std::byte>(weights_[i]);
    }
    return image;
}

float SplatCanvas::coverageAt(float distanceTiles, const Brush& brush)
{
    if (brush.radiusTiles <= 0.0f) {
        return 0.0f;
    }
    // Computed in texels, exactly as stamping does, so the rim's anti-aliasing
    // width is the same in the preview as on the canvas.
    const float scale = static_cast<float>(texelsPerTile);
    const float falloff = brushFalloff(
        distanceTiles * scale, brush.radiusTiles * scale, brush.hardness);
    return falloff * std::clamp(brush.opacity, 0.0f, 1.0f);
}

void SplatCanvas::beginStroke()
{
    if (empty()) {
        return;
    }
    strokeBase_ = weights_;
    strokeCoverage_.assign(weights_.size() / layerCount, 0);
    strokeActive_ = true;
}

void SplatCanvas::endStroke()
{
    strokeActive_ = false;
    strokeBase_.clear();
    strokeBase_.shrink_to_fit();
    strokeCoverage_.clear();
    strokeCoverage_.shrink_to_fit();
}

bool SplatCanvas::stamp(Vec2 centerTiles, const Brush& brush)
{
    const auto selectedLayer = static_cast<uint32_t>(brush.color);
    if (empty() || brush.radiusTiles <= 0.0f || brush.opacity <= 0.0f ||
        selectedLayer >= layerCount) {
        return false;
    }

    // A stamp outside a stroke is a stroke of exactly one stamp, so callers
    // that only want a single dab need not manage the lifecycle.
    const bool implicitStroke = !strokeActive_;
    if (implicitStroke) {
        beginStroke();
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
        if (implicitStroke) {
            endStroke();
        }
        return false;
    }

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

            // Strongest coverage wins, rather than accumulating: this is what
            // keeps a soft brush soft when the same spot is stamped on every
            // frame the button is held.
            const auto coverage = static_cast<uint8_t>(std::clamp(
                std::lround(falloff * opacity * 255.0f), 0L, 255L));
            if (coverage <= strokeCoverage_[index]) {
                continue;
            }
            strokeCoverage_[index] = coverage;

            // Composited against where the stroke started, never against its
            // own output.
            const std::size_t offset = index * layerCount;
            const uint32_t selectedBase = strokeBase_[offset + selectedLayer];
            const auto selectedWeight = static_cast<uint8_t>(selectedBase +
                ((255 - selectedBase) * coverage + 127) / 255);
            std::array<uint32_t, 4> others {};
            for (uint32_t layer = 0; layer < layerCount; ++layer) {
                if (layer != selectedLayer) others[layer] = strokeBase_[offset + layer];
            }
            std::array<uint8_t, 4> updated {};
            distributeWeights(others, 255 - selectedBase,
                255 - selectedWeight, updated.data());
            updated[selectedLayer] = selectedWeight;
            for (uint32_t layer = 0; layer < layerCount; ++layer) {
                if (updated[layer] != weights_[offset + layer]) {
                    weights_[offset + layer] = updated[layer];
                    changed = true;
                }
            }
        }
    }

    if (implicitStroke) {
        endStroke();
    }
    return changed;
}

bool SplatCanvas::stampLine(Vec2 fromTiles, Vec2 toTiles, const Brush& brush)
{
    if (empty() || brush.radiusTiles <= 0.0f) {
        return false;
    }

    // The stamps along a segment overlap heavily by design, so they have to
    // share one stroke's coverage or the line would paint darker than the
    // brush wherever they overlap.
    const bool implicitStroke = !strokeActive_;
    if (implicitStroke) {
        beginStroke();
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
    if (implicitStroke) {
        endStroke();
    }
    return changed;
}

bool SplatCanvas::resizeToBoard(
    uint32_t boardTilesWide, uint32_t boardTilesHigh, uint8_t fill)
{
    const uint32_t width = boardTilesWide * texelsPerTile;
    const uint32_t height = boardTilesHigh * texelsPerTile;
    if (width == 0 || height == 0) {
        return false;
    }
    if (width == width_ && height == height_) {
        return false;
    }

    std::vector<uint8_t> resized(
        static_cast<std::size_t>(width) * height * layerCount, 0);
    for (std::size_t i = 0; i < resized.size(); i += layerCount) {
        resized[i] = static_cast<uint8_t>(255 - fill);
        resized[i + 1] = fill;
    }
    // Anchored at the origin, which is where the board's origin is: tile
    // (0,0) keeps its paint whichever way the board grew or shrank.
    const uint32_t copyWidth = std::min(width, width_);
    const uint32_t copyHeight = std::min(height, height_);
    for (uint32_t y = 0; y < copyHeight; ++y) {
        const auto source = weights_.begin() +
            static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * width_ * layerCount);
        std::copy(
            source,
            source + static_cast<std::ptrdiff_t>(copyWidth * layerCount),
            resized.begin() +
                static_cast<std::ptrdiff_t>(
                    static_cast<std::size_t>(y) * width * layerCount));
    }

    width_ = width;
    height_ = height;
    weights_ = std::move(resized);
    // Stroke buffers are sized for the old canvas; continuing to composite
    // against them would index out of bounds.
    endStroke();
    return true;
}

bool SplatCanvas::restore(const std::vector<uint8_t>& snapshot)
{
    if (snapshot.size() != weights_.size()) {
        return false;
    }
    weights_ = snapshot;
    // Any stroke in progress was composited against the state just replaced,
    // so it cannot meaningfully continue.
    endStroke();
    return true;
}

} // namespace sokoban

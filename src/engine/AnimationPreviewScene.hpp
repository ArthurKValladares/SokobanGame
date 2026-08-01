#pragma once

#include "engine/render/RenderTypes.hpp"

namespace sokoban {

class AssetManifest;
class PresentationSettings;

namespace animationPreviewScene {

inline constexpr uint32_t bedSize = 3;
inline constexpr uint32_t bedCenter = bedSize / 2;
inline constexpr uint64_t animationInstanceId = 0x4150524556494557ULL;
inline constexpr Vec4 bedColor { 0.66f, 0.68f, 0.72f, 1.0f };
inline constexpr float cameraDistanceMultiplier = 4.0f;

// Builds an isolated authoring scene. The arbitrary source clip is supplied
// through the renderer preview hook; this frame owns the selected model,
// lighting, camera fit, floor, and stable skinned-instance identity.
[[nodiscard]] RenderFrameData build(
    RenderModel model,
    const AssetManifest& manifest,
    const PresentationSettings& settings);

} // namespace animationPreviewScene
} // namespace sokoban

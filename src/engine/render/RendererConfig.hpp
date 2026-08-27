#pragma once

#include <cstdint>

namespace sokoban::config {

inline constexpr float maxWireframeLineWidth = 16.0f;
// The heap may be smaller on lower-limit devices, but never larger than this.
// Reserves are admission-control headroom: they keep a manifest that merely
// fits at startup from exhausting stable handles as soon as editor or import
// tooling appends content.
inline constexpr uint32_t textureDescriptorCapacityCeiling = 1024;
inline constexpr uint32_t editorTextureDescriptorReserve = 16;
inline constexpr uint32_t importedTextureDescriptorReserve = 32;

} // namespace sokoban::config

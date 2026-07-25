#pragma once

namespace sokoban::config {

// Defaults for a newly created player profile. Runtime values are persisted.
inline constexpr float masterVolume = 0.03f;
inline constexpr float musicVolume = 0.5f; // relative to master
inline constexpr float soundVolume = 1.0f;
inline constexpr float minimumVolume = 0.0f;
inline constexpr float maximumVolume = 1.0f;

// Per-sound-set volumes live in assets/manifest.json (sound entries).
inline constexpr float footstepIntervalSeconds = 0.2f;
inline constexpr float minimumFootstepIntervalSeconds = 0.01f;
inline constexpr float maximumFootstepIntervalSeconds = 1.0f;
inline constexpr int maximumFootstepsPerUpdate = 4;

} // namespace sokoban::config

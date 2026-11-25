#pragma once

#include <filesystem>

namespace sokoban {

// Returns the validated read-only content root installed beside the current
// executable.
[[nodiscard]] std::filesystem::path runtimeContentRoot(
    const std::filesystem::path& sourceLevelRoot = {});

// The two halves of runtimeContentRoot(), for a caller that validates off the
// main thread. The path comes from SDL, which caches it in a global on first
// use, so resolve it on the thread that owns SDL; it needs no SDL_Init.
[[nodiscard]] std::filesystem::path runtimeContentRootPath();
// Recovers an interrupted editor publication under `sourceLevelRoot`, when one
// is given, then validates the package at `root`. Reads and writes only the
// filesystem.
void validateRuntimeContent(
    const std::filesystem::path& root,
    const std::filesystem::path& sourceLevelRoot = {});

} // namespace sokoban

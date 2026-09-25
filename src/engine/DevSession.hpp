#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace sokoban {

// Where a developer left off, so a Debug launch can pick up there instead of
// at the title screen. Written by Debug builds with developer tools when the
// application exits; never read or written by player builds.
//
// The game location itself is not stored here: the active save slot already
// records it, and resuming simply continues that slot. This only carries what
// the save does not: whether to skip the title at all, and the editor state.
struct DevSession {
    // Continue the active slot on launch instead of showing the title.
    bool resumeOnLaunch = true;
    // The level document open in the editor, as a source path. Empty when the
    // editor had nothing open.
    std::filesystem::path editorDocument;
    // Whether the editor view (rather than gameplay) was showing.
    bool editingDocument = false;
    int activeLayer = 0;
    // "tiles", "decorations", or "selectors".
    std::string tool = "tiles";
};

// Returns nullopt when the file is missing or unreadable. A damaged file is
// not an error worth stopping a launch for; the caller starts normally.
[[nodiscard]] std::optional<DevSession> loadDevSession(
    const std::filesystem::path& path);

// Atomic replacement. Throws on failure.
void saveDevSession(const std::filesystem::path& path, const DevSession& session);

} // namespace sokoban

#pragma once

#include "engine/AssetManifestEditor.hpp"

#include <cstddef>
#include <memory>
#include <optional>

struct SDL_Window;

namespace sokoban {

class AudioSystem;

// ImGui adapter for AssetManifestEditor. It owns only window interaction state;
// all document mutations, validation, and filesystem work stay headless.
class AssetManifestDebugUi {
public:
    void draw(AssetManifestEditor& editor, AudioSystem& audio, SDL_Window* window);

private:
    struct ItemAction {
        int moveDirection = 0;
        bool remove = false;
    };

    [[nodiscard]] ItemAction drawItemActions(std::size_t index, std::size_t count) const;
    void drawTextures(AssetManifestEditor& editor);
    void drawModels(AssetManifestEditor& editor);
    void drawAnimations(AssetManifestEditor& editor);
    void drawTiles(AssetManifestEditor& editor);
    void drawSounds(AssetManifestEditor& editor, AudioSystem& audio);
    void drawMusic(AssetManifestEditor& editor);

    struct SoundFileRequest {
        std::size_t soundIndex;
        std::size_t fileIndex;
    };
    struct SoundFileDialog;
    void beginSoundFileDialog(AssetManifestEditor& editor, SDL_Window* window);
    void finishSoundFileDialog(AssetManifestEditor& editor);

    bool reloadConfirmationOpen_ = false;
    std::optional<SoundFileRequest> soundFileRequest_;
    std::shared_ptr<SoundFileDialog> soundFileDialog_;
    std::string soundStatus_;
};

} // namespace sokoban

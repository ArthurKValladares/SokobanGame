#pragma once

#include "engine/AssetManifestEditor.hpp"

#include <cstddef>

namespace sokoban {

// ImGui adapter for AssetManifestEditor. It owns only window interaction state;
// all document mutations, validation, and filesystem work stay headless.
class AssetManifestDebugUi {
public:
    void draw(AssetManifestEditor& editor);

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
    void drawSounds(AssetManifestEditor& editor);
    void drawMusic(AssetManifestEditor& editor);

    bool reloadConfirmationOpen_ = false;
};

} // namespace sokoban

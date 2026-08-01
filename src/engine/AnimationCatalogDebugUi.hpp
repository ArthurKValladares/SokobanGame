#pragma once

#include <filesystem>
#include <string>

namespace sokoban {

class AnimationCatalog;
class AnimationPreviewDebugUi;
class AssetManifest;
class VulkanRenderer;

class AnimationCatalogDebugUi {
public:
    void initialize(std::filesystem::path filePath);

    // Returns true when the live catalog changed.
    bool draw(
        AnimationCatalog& catalog,
        const AssetManifest& manifest,
        AnimationPreviewDebugUi& preview,
        VulkanRenderer& renderer);

private:
    std::filesystem::path filePath_;
    std::string status_;
    bool dirty_ = false;
};

} // namespace sokoban

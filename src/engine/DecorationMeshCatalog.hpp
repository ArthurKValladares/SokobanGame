#pragma once

#include "engine/AssetManifest.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sokoban {

// Debug-authoring catalog for mesh files in the source asset tree.
//
// Runtime screens reference stable manifest model names and never use this
// scanner. Unregistered files remain visible to the editor, but cannot be
// placed until their model definition exists in assets/manifest.json.
class DecorationMeshCatalog {
public:
    struct Entry {
        std::filesystem::path relativePath;
        std::string modelName;

        [[nodiscard]] bool registered() const
        {
            return !modelName.empty();
        }
    };

    [[nodiscard]] bool refresh(
        const std::filesystem::path& sourceAssetRoot,
        const AssetManifest& manifest);

    [[nodiscard]] const std::vector<Entry>& entries() const
    {
        return entries_;
    }
    [[nodiscard]] const std::string& status() const { return status_; }

private:
    std::vector<Entry> entries_;
    std::string status_;
};

} // namespace sokoban

#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/AssetManifestEditor.hpp"

#include <filesystem>
#include <string>

namespace sokoban {

// Debug-authoring bridge from arbitrary source meshes to the strict runtime
// manifest. Shipping screens continue to store stable model names; selecting
// a source mesh creates that name and stages the referenced files once.
class DecorationAssetRegistry {
public:
    struct Request {
        std::filesystem::path sourceAssetRoot;
        std::filesystem::path runtimeAssetRoot;
        std::filesystem::path relativeMeshPath;
        AssetManifest& runtimeManifest;
        AssetManifestEditor& manifestEditor;
    };

    struct Result {
        bool succeeded = false;
        bool added = false;
        std::string modelName;
        std::string status;
    };

    [[nodiscard]] static Result registerMesh(const Request& request);
};

} // namespace sokoban

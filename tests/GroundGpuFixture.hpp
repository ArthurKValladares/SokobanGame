#pragma once

#include "ScopedTestDirectory.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/GroundTileGeometry.hpp"
#include "engine/render/GroundMeshGeometry.hpp"
#include "engine/ui/UiConfig.hpp"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifndef SOKOBAN_TEST_SOURCE_DIR
#error "Ground GPU fixtures need the source asset directory"
#endif

// The shipped GroundRock names now refer to native bodies. Exercise the old
// processor with its real registered name/path contract in an isolated package,
// without changing the shipped manifest or relying on unregistered staged art.
class LegacyGroundGpuFixture final {
public:
    LegacyGroundGpuFixture(const std::filesystem::path& runtimeAssets,
        const sokoban::AssetManifest& current)
        : directory_("legacy-ground-gpu")
        , root_(directory_.path() / "assets")
    {
        using namespace sokoban;
        const auto sourceAssets = std::filesystem::path(SOKOBAN_TEST_SOURCE_DIR) / "assets";
        constexpr std::array<std::string_view, 10> textureNames {
            "GroundGrass", "GroundRock", "GroundSplatMap",
            "GroundGrassNormal", "GroundGrassOrm", "GroundRockNormal", "GroundRockOrm",
            "GroundRockSide", "GroundRockSideNormal", "GroundRockSideOrm",
        };
        for (const auto name : textureNames) {
            const auto id = current.textureIdByName(name);
            const auto& texture = current.textures().at(id.index());
            if (manifest_.addTexture(texture).isNone()) {
                throw std::runtime_error("Could not register legacy ground fixture texture");
            }
            copyAsset(sourceAssets, texture.path);
        }

        auto legacy = current.model(current.modelIdByName("GroundRock01"));
        legacy.path = "custom/pbr/models/GroundRock01.glb";
        if (!isProcessableGroundRockModel(legacy) || authoredGroundTileVariant(legacy)) {
            throw std::runtime_error("Legacy ground GPU fixture lost its processing contract");
        }
        copyAsset(sourceAssets, legacy.path);
        for (const auto& dependency : resolveGltfExternalFiles(sourceAssets, legacy.path,
                 "legacy ground GPU fixture")) {
            copyAsset(sourceAssets, dependency);
        }
        if (manifest_.addModel(std::move(legacy)).isCube()) {
            throw std::runtime_error("Could not register legacy ground fixture model");
        }

        std::filesystem::copy(runtimeAssets / "shaders", root_ / "shaders",
            std::filesystem::copy_options::recursive);
        copyAsset(runtimeAssets, std::filesystem::path(config::titleBackgroundPath));
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] const sokoban::AssetManifest& manifest() const { return manifest_; }

private:
    void copyAsset(const std::filesystem::path& sourceRoot,
        const std::filesystem::path& relative)
    {
        const auto destination = root_ / relative;
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(sourceRoot / relative, destination,
            std::filesystem::copy_options::overwrite_existing);
    }

    ScopedTestDirectory directory_;
    std::filesystem::path root_;
    sokoban::AssetManifest manifest_;
};

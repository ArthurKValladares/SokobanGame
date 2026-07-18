#pragma once

#include "engine/AssetManifest.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace sokoban {

// Headless editable document for assets/manifest.json. UI adapters issue
// mutations through this API; parsing, validation, dirty state, and filesystem
// replacement remain independent of ImGui.
class AssetManifestEditor {
public:
    void initialize(std::filesystem::path filePath);
    [[nodiscard]] bool load(std::filesystem::path filePath);
    [[nodiscard]] bool reload();
    [[nodiscard]] bool validate();
    [[nodiscard]] bool save();

    [[nodiscard]] const std::filesystem::path& filePath() const { return filePath_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] std::string serialize() const;

    [[nodiscard]] const std::vector<AssetManifest::Texture>& textures() const { return textures_; }
    [[nodiscard]] const std::vector<AssetManifest::Model>& models() const { return models_; }
    [[nodiscard]] const std::vector<AssetManifest::Animation>& animations() const { return animations_; }
    [[nodiscard]] const std::vector<AssetManifest::TileEntry>& tileEntries() const { return tiles_; }
    [[nodiscard]] const std::vector<AssetManifest::SoundSet>& soundSets() const { return sounds_; }
    [[nodiscard]] const std::vector<AssetManifest::MusicTrack>& musicTracks() const { return music_; }

    void updateTexture(std::size_t index, AssetManifest::Texture texture);
    void updateModel(std::size_t index, AssetManifest::Model model);
    void updateAnimation(std::size_t index, AssetManifest::Animation animation);
    void updateTile(std::size_t index, AssetManifest::TileEntry tile);
    void updateSoundSet(std::size_t index, AssetManifest::SoundSet sound);
    void updateMusicTrack(std::size_t index, AssetManifest::MusicTrack track);

    void addTexture();
    void addModel();
    void addAnimation();
    void addTile();
    void addSoundSet();
    void addMusicTrack();

    [[nodiscard]] bool removeTexture(std::size_t index);
    [[nodiscard]] bool removeModel(std::size_t index);
    [[nodiscard]] bool removeAnimation(std::size_t index);
    [[nodiscard]] bool removeTile(std::size_t index);
    [[nodiscard]] bool removeSoundSet(std::size_t index);
    [[nodiscard]] bool removeMusicTrack(std::size_t index);

    [[nodiscard]] bool moveTexture(std::size_t index, int direction);
    [[nodiscard]] bool moveModel(std::size_t index, int direction);
    [[nodiscard]] bool moveAnimation(std::size_t index, int direction);
    [[nodiscard]] bool moveTile(std::size_t index, int direction);
    [[nodiscard]] bool moveSoundSet(std::size_t index, int direction);
    [[nodiscard]] bool moveMusicTrack(std::size_t index, int direction);

private:
    void markChanged();

    std::filesystem::path filePath_;
    std::vector<AssetManifest::Texture> textures_;
    std::vector<AssetManifest::Model> models_;
    std::vector<AssetManifest::Animation> animations_;
    std::vector<AssetManifest::TileEntry> tiles_;
    std::vector<AssetManifest::SoundSet> sounds_;
    std::vector<AssetManifest::MusicTrack> music_;
    std::string status_;
    bool dirty_ = false;
};

} // namespace sokoban

#include "engine/AssetManifestDebugUi.hpp"

#include "engine/TileTypes.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_stdlib.h>
#endif

namespace sokoban {
namespace {

#if SOKOBAN_ENABLE_DEBUG_UI
bool drawGeometry(ModelGeometry& geometry)
{
    constexpr std::array labels { "Static", "Skinned" };
    int selected = geometry == ModelGeometry::Skinned ? 1 : 0;
    if (!ImGui::Combo("Geometry", &selected, labels.data(), static_cast<int>(labels.size()))) {
        return false;
    }
    geometry = selected == 1 ? ModelGeometry::Skinned : ModelGeometry::Static;
    return true;
}

bool drawMaterialMode(ModelMaterialMode& mode)
{
    constexpr std::array labels { "None", "Texture", "Primitive Texture Index" };
    int selected = static_cast<int>(mode);
    if (!ImGui::Combo("Material", &selected, labels.data(), static_cast<int>(labels.size()))) {
        return false;
    }
    mode = static_cast<ModelMaterialMode>(selected);
    return true;
}

bool drawTextureReference(
    std::string& textureName,
    const std::vector<AssetManifest::Texture>& textures)
{
    const char* preview = textureName.empty() ? "<Select Texture>" : textureName.c_str();
    bool changed = false;
    if (ImGui::BeginCombo("Texture", preview)) {
        for (const AssetManifest::Texture& texture : textures) {
            const bool selected = textureName == texture.name;
            if (ImGui::Selectable(texture.name.c_str(), selected)) {
                textureName = texture.name;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool drawAnimationRole(std::string& role)
{
    constexpr std::array values { "", "player-idle", "player-move", "player-push" };
    constexpr std::array labels { "None", "Player Idle", "Player Move", "Player Push" };
    int selected = 0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (role == values[i]) {
            selected = static_cast<int>(i);
            break;
        }
    }
    if (!ImGui::Combo("Role", &selected, labels.data(), static_cast<int>(labels.size()))) {
        return false;
    }
    role = values[static_cast<std::size_t>(selected)];
    return true;
}

bool drawTileType(TileType& tile)
{
    const std::string preview(tileTypeName(tile));
    bool changed = false;
    if (ImGui::BeginCombo("Tile", preview.c_str())) {
        for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
            const bool selected = tile == definition.type;
            if (ImGui::Selectable(definition.name.data(), selected)) {
                tile = definition.type;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool drawModelReference(
    std::string& modelName,
    const std::vector<AssetManifest::Model>& models)
{
    const char* preview = modelName.empty() ? "Procedural Cube" : modelName.c_str();
    bool changed = false;
    if (ImGui::BeginCombo("Model", preview)) {
        const bool cubeSelected = modelName.empty();
        if (ImGui::Selectable("Procedural Cube", cubeSelected)) {
            modelName.clear();
            changed = true;
        }
        if (cubeSelected) {
            ImGui::SetItemDefaultFocus();
        }
        for (const AssetManifest::Model& model : models) {
            const bool selected = modelName == model.name;
            if (ImGui::Selectable(model.name.c_str(), selected)) {
                modelName = model.name;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawEmptySection(const char* label)
{
    ImGui::TextDisabled("No %s entries.", label);
}
#endif

} // namespace

void AssetManifestDebugUi::draw(AssetManifestEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::TextUnformatted(editor.filePath().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(editor.dirty() ? "modified" : "clean");

    if (ImGui::Button("Save")) {
        (void)editor.save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate")) {
        (void)editor.validate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        if (editor.dirty()) {
            reloadConfirmationOpen_ = true;
        } else {
            (void)editor.reload();
        }
    }

    constexpr const char* popupName = "Discard Manifest Changes?";
    if (reloadConfirmationOpen_) {
        ImGui::OpenPopup(popupName);
    }
    if (ImGui::BeginPopupModal(
            popupName,
            &reloadConfirmationOpen_,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Reload the manifest and discard unsaved changes?");
        ImGui::Separator();
        if (ImGui::Button("Discard", ImVec2(90.0f, 0.0f))) {
            (void)editor.reload();
            reloadConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            reloadConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!editor.status().empty()) {
        ImGui::TextWrapped("%s", editor.status().c_str());
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("ManifestSections")) {
        if (ImGui::BeginTabItem("Textures")) {
            drawTextures(editor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Models")) {
            drawModels(editor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Animations")) {
            drawAnimations(editor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tiles")) {
            drawTiles(editor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sounds")) {
            drawSounds(editor);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Music")) {
            drawMusic(editor);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#else
    (void)editor;
#endif
}

AssetManifestDebugUi::ItemAction AssetManifestDebugUi::drawItemActions(
    std::size_t index,
    std::size_t count) const
{
    ItemAction action;
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::SameLine();
    ImGui::BeginDisabled(index == 0);
    if (ImGui::SmallButton("Up")) {
        action.moveDirection = -1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= count);
    if (ImGui::SmallButton("Down")) {
        action.moveDirection = 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    action.remove = ImGui::SmallButton("Delete");
#else
    (void)index;
    (void)count;
#endif
    return action;
}

void AssetManifestDebugUi::drawTextures(AssetManifestEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (ImGui::Button("+ Texture")) {
        editor.addTexture();
    }
    if (editor.textures().empty()) {
        drawEmptySection("texture");
    }
    for (std::size_t i = 0; i < editor.textures().size(); ++i) {
        AssetManifest::Texture texture = editor.textures()[i];
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNode(texture.name.c_str());
        const ItemAction action = drawItemActions(i, editor.textures().size());
        if (action.remove) {
            (void)editor.removeTexture(i);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (action.moveDirection != 0) {
            (void)editor.moveTexture(i, action.moveDirection);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (open) {
            bool changed = ImGui::InputText("Name", &texture.name);
            changed = ImGui::InputText("Path", &texture.path) || changed;
            if (changed) {
                editor.updateTexture(i, std::move(texture));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
#else
    (void)editor;
#endif
}

void AssetManifestDebugUi::drawModels(AssetManifestEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (ImGui::Button("+ Model")) {
        editor.addModel();
    }
    if (editor.models().empty()) {
        drawEmptySection("model");
    }
    for (std::size_t i = 0; i < editor.models().size(); ++i) {
        AssetManifest::Model model = editor.models()[i];
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNode(model.name.c_str());
        const ItemAction action = drawItemActions(i, editor.models().size());
        if (action.remove) {
            (void)editor.removeModel(i);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (action.moveDirection != 0) {
            (void)editor.moveModel(i, action.moveDirection);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (open) {
            bool changed = ImGui::InputText("Name", &model.name);
            changed = ImGui::InputText("Path", &model.path) || changed;
            changed = drawGeometry(model.geometry) || changed;
            changed = drawMaterialMode(model.materialMode) || changed;
            if (model.materialMode == ModelMaterialMode::SingleTexture) {
                changed = drawTextureReference(model.materialTextureName, editor.textures()) || changed;
            } else if (model.materialMode == ModelMaterialMode::PrimitiveTextureIndex) {
                int index = static_cast<int>(std::min<uint32_t>(
                    model.textureIndex,
                    static_cast<uint32_t>(std::numeric_limits<int>::max())));
                if (ImGui::InputInt("Texture Index", &index)) {
                    model.textureIndex = static_cast<uint32_t>(std::max(index, 0));
                    changed = true;
                }
            }
            changed = ImGui::Checkbox("Preserve Aspect Ratio", &model.preserveAspectRatio) || changed;
            changed = ImGui::Checkbox("Rotate Half Turn", &model.rotateHalfTurn) || changed;
            changed = ImGui::Checkbox("Belt Scroll", &model.beltScroll) || changed;
            changed = ImGui::Checkbox("Player Role", &model.playerRole) || changed;
            if (changed) {
                model.primitiveTextures =
                    model.materialMode == ModelMaterialMode::PrimitiveTextureIndex;
                editor.updateModel(i, std::move(model));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
#else
    (void)editor;
#endif
}

void AssetManifestDebugUi::drawAnimations(AssetManifestEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (ImGui::Button("+ Animation")) {
        editor.addAnimation();
    }
    if (editor.animations().empty()) {
        drawEmptySection("animation");
    }
    for (std::size_t i = 0; i < editor.animations().size(); ++i) {
        AssetManifest::Animation animation = editor.animations()[i];
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNode(animation.name.c_str());
        const ItemAction action = drawItemActions(i, editor.animations().size());
        if (action.remove) {
            (void)editor.removeAnimation(i);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (action.moveDirection != 0) {
            (void)editor.moveAnimation(i, action.moveDirection);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (open) {
            bool changed = ImGui::InputText("Name", &animation.name);
            changed = ImGui::InputText("Path", &animation.path) || changed;
            int clip = static_cast<int>(std::min<uint32_t>(
                animation.clip,
                static_cast<uint32_t>(std::numeric_limits<int>::max())));
            if (ImGui::InputInt("Clip", &clip)) {
                animation.clip = static_cast<uint32_t>(std::max(clip, 0));
                changed = true;
            }
            changed = drawAnimationRole(animation.role) || changed;
            if (changed) {
                editor.updateAnimation(i, std::move(animation));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
#else
    (void)editor;
#endif
}

void AssetManifestDebugUi::drawTiles(AssetManifestEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (ImGui::Button("+ Tile Visual")) {
        editor.addTile();
    }
    if (editor.tileEntries().empty()) {
        drawEmptySection("tile visual");
    }
    for (std::size_t i = 0; i < editor.tileEntries().size(); ++i) {
        AssetManifest::TileEntry tile = editor.tileEntries()[i];
        const std::string label(tileTypeName(tile.tile));
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNode(label.c_str());
        const ItemAction action = drawItemActions(i, editor.tileEntries().size());
        if (action.remove) {
            (void)editor.removeTile(i);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (action.moveDirection != 0) {
            (void)editor.moveTile(i, action.moveDirection);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (open) {
            bool changed = drawTileType(tile.tile);
            changed = drawModelReference(tile.modelName, editor.models()) || changed;
            if (ImGui::DragFloat("Scale", &tile.scale, 0.01f, 0.01f, 10.0f, "%.2f")) {
                tile.scale = std::max(tile.scale, 0.01f);
                changed = true;
            }
            if (changed) {
                editor.updateTile(i, std::move(tile));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
#else
    (void)editor;
#endif
}

void AssetManifestDebugUi::drawSounds(AssetManifestEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (ImGui::Button("+ Sound Set")) {
        editor.addSoundSet();
    }
    if (editor.soundSets().empty()) {
        drawEmptySection("sound set");
    }
    for (std::size_t i = 0; i < editor.soundSets().size(); ++i) {
        AssetManifest::SoundSet sound = editor.soundSets()[i];
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNode(sound.name.c_str());
        const ItemAction action = drawItemActions(i, editor.soundSets().size());
        if (action.remove) {
            (void)editor.removeSoundSet(i);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (action.moveDirection != 0) {
            (void)editor.moveSoundSet(i, action.moveDirection);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (open) {
            bool changed = ImGui::InputText("Name", &sound.name);
            if (ImGui::DragFloat("Volume", &sound.volume, 0.01f, 0.0f, 4.0f, "%.2f")) {
                sound.volume = std::max(sound.volume, 0.0f);
                changed = true;
            }
            ImGui::SeparatorText("Files");
            for (std::size_t fileIndex = 0; fileIndex < sound.files.size(); ++fileIndex) {
                ImGui::PushID(static_cast<int>(fileIndex));
                ImGui::SetNextItemWidth(-40.0f);
                changed = ImGui::InputText("##File", &sound.files[fileIndex]) || changed;
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    sound.files.erase(
                        sound.files.begin() + static_cast<std::ptrdiff_t>(fileIndex));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Remove file");
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ File")) {
                sound.files.push_back("audio/sound.ogg");
                changed = true;
            }
            if (changed) {
                editor.updateSoundSet(i, std::move(sound));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
#else
    (void)editor;
#endif
}

void AssetManifestDebugUi::drawMusic(AssetManifestEditor& editor)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (ImGui::Button("+ Music Track")) {
        editor.addMusicTrack();
    }
    if (editor.musicTracks().empty()) {
        drawEmptySection("music track");
    }
    for (std::size_t i = 0; i < editor.musicTracks().size(); ++i) {
        AssetManifest::MusicTrack track = editor.musicTracks()[i];
        const std::string label = "Level " + std::to_string(track.level);
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNode(label.c_str());
        const ItemAction action = drawItemActions(i, editor.musicTracks().size());
        if (action.remove) {
            (void)editor.removeMusicTrack(i);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (action.moveDirection != 0) {
            (void)editor.moveMusicTrack(i, action.moveDirection);
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            break;
        }
        if (open) {
            bool changed = false;
            if (ImGui::InputInt("Level", &track.level)) {
                track.level = std::max(track.level, 0);
                changed = true;
            }
            changed = ImGui::InputText("File", &track.file) || changed;
            if (ImGui::DragFloat("Volume", &track.volume, 0.01f, 0.0f, 4.0f, "%.2f")) {
                track.volume = std::max(track.volume, 0.0f);
                changed = true;
            }
            if (changed) {
                editor.updateMusicTrack(i, std::move(track));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
#else
    (void)editor;
#endif
}

} // namespace sokoban

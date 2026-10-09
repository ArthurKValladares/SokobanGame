// Exercise the editor's actual ImGui buttons with mouse events. No native
// window or GPU is needed, and all editor files stay in a temporary project.
#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/EditorTilePalette.hpp"
#include "engine/LevelEditorDebugUi.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace sokoban;

class EditorPanel {
public:
    struct Widget {
        ImGuiID id = 0;
        ImGuiWindow* window = nullptr;
    };

    struct Icon {
        TileType tile;
        Widget widget;
    };

    explicit EditorPanel(LevelEditor::Tool tool = LevelEditor::Tool::Selectors)
        : previousContext_(ImGui::GetCurrentContext()),
          context_(ImGui::CreateContext()),
          manifest_(AssetManifest::parse(R"json({
            "format": 1,
            "textures": [
                { "name": "GroundGrass", "path": "grass.png" },
                { "name": "GroundRock", "path": "rock.png" },
                { "name": "InitialMask", "path": "initial-mask.png", "colorSpace": "linear" },
                { "name": "AddedMask", "path": "added-mask.png", "colorSpace": "linear" },
                { "name": "NewMask", "path": "new-mask.png", "colorSpace": "linear" }
            ],
            "models": [
                { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }
            ],
            "animations": [
                { "name": "Idle", "path": "a.glb", "role": "player-idle" },
                { "name": "Move", "path": "a.glb", "role": "player-move" },
                { "name": "Push", "path": "a.glb", "role": "player-push" },
                { "name": "Death", "path": "a.glb", "role": "player-death" },
                { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" }
            ]
        })json"))
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = { 1600.0f, 6000.0f };
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->Build();
        io.Fonts->SetTexID(1);

        std::filesystem::create_directories(project_.path() / "levels");
        editor.initialize(project_.path() / "levels", project_.path() / "runtime-levels", 0, 0);
        editor.newDocument(4, 3, false);
        editor.setTool(tool);
        if (!editor.addGroundSplat({
                .name = "Ground", .base = "GroundGrass", .detail = "GroundRock",
                .mask = "InitialMask", .color = { 0.2f, 0.8f, 0.3f } })) {
            throw std::runtime_error("Could not initialize splat UI fixture");
        }
        ui_.initialize(editor);
        callbacks_.tileThumbnail = [this](TileType tile) -> uint64_t {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            // The adapter requests the thumbnail inside the family's ID
            // scope, immediately before drawPaintButton adds the tile ID.
            if (std::string_view(window->Name) == "Editor regression" && window->IDStack.Size >= 3) {
                paletteScope_ = window->IDStack[window->IDStack.Size - 2];
            }
            ImGui::PushID(static_cast<int>(tile));
            const ImGuiID id = ImGui::GetID("##paint_tile");
            ImGui::PopID();
            icons_.push_back({ tile, { id, window } });
            return 0;
        };
        callbacks_.assetManifest = [this]() -> const AssetManifest& { return manifest_; };
        callbacks_.createGroundSplatMap = [this] {
            ++addCalls;
            if (!editor.addGroundSplat({
                    .name = "Ground 2", .base = "GroundRock", .detail = "GroundGrass",
                    .mask = "AddedMask", .color = { 0.8f, 0.2f, 0.3f } })) {
                return false;
            }
            painter.close();
            editor.setGroundAssignmentPainting(true);
            return true;
        };
        callbacks_.openGroundPainting = [this] {
            ++paintCalls;
            return openSelectedMask();
        };
        callbacks_.createGroundBlendMask = [this] {
            ++newMaskCalls;
            const auto* selected = editor.selectedGroundSplat();
            if (!selected) return false;
            const auto index = static_cast<std::size_t>(selected - editor.groundSplats().data());
            auto edited = *selected;
            edited.mask = "NewMask";
            if (!editor.updateGroundSplat(index, std::move(edited))) return false;
            return openSelectedMask();
        };
        // Tab selection and first-window sizing settle on the following frame.
        (void)frame();
        (void)frame();
    }

    ~EditorPanel()
    {
        ImGui::DestroyContext(context_);
        ImGui::SetCurrentContext(previousContext_);
    }

    struct Frame {
        std::string text;
        std::optional<ImVec2> buttonCenter;
        std::vector<Icon> icons;
    };

    Frame frame(const char* locateButton = nullptr, std::optional<Widget> widget = std::nullopt)
    {
        ImGui::SetCurrentContext(context_);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ 1500, 5900 }, ImGuiCond_Always);
        ImGui::Begin("Editor regression", nullptr,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove);
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGuiWindow* targetWindow = widget ? widget->window : window;
        const ImGuiID buttonId = widget ? widget->id : locateButton ? ImGui::GetID(locateButton) : 0;
        if (buttonId != 0) {
            // Navigation records the real item bounds by ID, so tests need no
            // hard-coded pixel positions or special production-side hooks.
            context_->NavWindow = targetWindow;
            ImGui::SetNavID(buttonId, ImGuiNavLayer_Main,
                targetWindow->NavRootFocusScopeId, ImRect());
            context_->NavIdIsAlive = false;
        }
        icons_.clear();
        // Respect collapsed sections while recording their visible text.
        ImGui::LogToBuffer(0);
        ui_.draw(editor, overworld_, painter, bindings_, callbacks_);
        Frame result { .text = context_->LogBuffer.c_str(), .icons = icons_ };
        if (buttonId != 0 && context_->NavId == buttonId && context_->NavIdIsAlive) {
            const ImRect rect = ImGui::WindowRectRelToAbs(targetWindow,
                targetWindow->NavRectRel[ImGuiNavLayer_Main]);
            result.buttonCenter = rect.GetCenter();
        }
        ImGui::LogFinish();
        ImGui::End();
        ImGui::Render();
        return result;
    }

    void click(const char* label)
    {
        const auto located = frame(label);
        if (!located.buttonCenter || located.text.find(label) == std::string::npos) {
            throw std::runtime_error(std::string("Missing editor button: ") + label);
        }
        clickAt(*located.buttonCenter);
    }

    void clickPaletteControl(const char* label)
    {
        (void)frame();
        ImGuiWindow* window = ImGui::FindWindowByName("Editor regression");
        const auto located = frame(nullptr, Widget { ImHashStr(label, 0, paletteScope_), window });
        const std::string_view text(label);
        const auto visibleLabel = text.substr(0, text.find("##"));
        if (!located.buttonCenter || located.text.find(visibleLabel) == std::string::npos) {
            throw std::runtime_error(std::string("Missing palette control: ") + label);
        }
        clickAt(*located.buttonCenter);
    }

    void clickIcon(TileType tile, bool inPopup = false)
    {
        const auto visible = frame();
        const auto found = std::ranges::find_if(visible.icons, [&](const Icon& icon) {
            const bool popup = (icon.widget.window->Flags & ImGuiWindowFlags_Popup) != 0;
            return icon.tile == tile && popup == inPopup;
        });
        if (found == visible.icons.end()) throw std::runtime_error("Missing palette icon");
        const auto located = frame(nullptr, found->widget);
        if (!located.buttonCenter) throw std::runtime_error("Palette icon has no clickable bounds");
        clickAt(*located.buttonCenter);
    }

    void clickAt(ImVec2 center)
    {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(center.x, center.y);
        (void)frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        (void)frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        (void)frame();
    }

    bool shows(std::string_view label)
    {
        return frame().text.find(label) != std::string::npos;
    }

    RenderTexture maskTexture(std::string_view name) const
    {
        return manifest_.findTextureIdByName(name);
    }

    LevelEditor editor;
    SplatPainter painter;
    int addCalls = 0;
    int paintCalls = 0;
    int newMaskCalls = 0;

private:
    bool openSelectedMask()
    {
        const auto* selected = editor.selectedGroundSplat();
        if (!selected) return false;
        editor.setGroundAssignmentPainting(false);
        editor.showGroundAssignmentColors() = false;
        return painter.open({
            .documentPath = project_.path() / "levels" / "level0" / "screen0.scr",
            .boardTilesWide = editor.documentWidth(),
            .boardTilesHigh = editor.documentHeight(),
            .sourceAssetRoot = project_.path() / "assets",
            .textureName = selected->mask,
        }, manifest_);
    }

    ImGuiContext* previousContext_;
    ImGuiContext* context_;
    ScopedTestDirectory project_ { "splat-ui-regression" };
    AssetManifest manifest_;
    OverworldMapEditor overworld_;
    LevelEditorDebugUi ui_;
    InputBindings bindings_ = defaultInputBindings();
    LevelEditorDebugUi::Callbacks callbacks_;
    std::vector<Icon> icons_;
    ImGuiID paletteScope_ = 0;
};

void addSplatKeepsMaskPaintingAvailable()
{
    TEST("addSplatKeepsMaskPaintingAvailable");
    EditorPanel panel;
    CHECK(panel.shows("Paint Blend Mask"));
    panel.click("Add Splat Map");
    CHECK(panel.addCalls == 1);
    CHECK(panel.editor.groundSplats().size() == 2);
    CHECK(panel.editor.selectedGroundSplat()->name == "Ground 2");
    CHECK(panel.editor.groundAssignmentPainting());
    CHECK(panel.shows("Paint Blend Mask"));
    panel.click("Paint Blend Mask");
    CHECK(panel.paintCalls == 1);
    CHECK(!panel.editor.groundAssignmentPainting());
    CHECK(panel.painter.active());
    CHECK(panel.painter.texture() == panel.maskTexture("AddedMask"));
    CHECK(panel.shows("Stop Painting"));
    CHECK(panel.shows("Save Map"));

    // Assignment painting can also be re-entered independently of Add.
    panel.click("Paint Tile Assignments");
    CHECK(panel.editor.groundAssignmentPainting());
    CHECK(!panel.painter.active());
    CHECK(panel.shows("Paint Blend Mask"));
    panel.click("Paint Blend Mask");
    CHECK(panel.paintCalls == 2);
    CHECK(!panel.editor.groundAssignmentPainting());
    CHECK(panel.painter.active());
}

void newMaskActionUsesSelectedSplat()
{
    TEST("newMaskActionUsesSelectedSplat");
    EditorPanel panel;
    panel.click("Add Splat Map");
    CHECK(panel.editor.groundAssignmentPainting());
    CHECK(panel.shows("New Blend Mask"));
    panel.click("New Blend Mask");
    CHECK(panel.newMaskCalls == 1);
    CHECK(panel.editor.groundSplats().size() == 2);
    CHECK(panel.editor.groundSplats()[0].mask == "InitialMask");
    CHECK(panel.editor.selectedGroundSplat()->mask == "NewMask");
    CHECK(!panel.editor.groundAssignmentPainting());
    CHECK(panel.painter.active());
    CHECK(panel.painter.texture() == panel.maskTexture("NewMask"));
    CHECK(panel.shows("Stop Painting"));
}

bool iconIsInPopup(const EditorPanel::Icon& icon)
{
    return (icon.widget.window->Flags & ImGuiWindowFlags_Popup) != 0;
}

void terrainSectionCollapsesWithoutHidingQuarryWalls()
{
    TEST("terrainSectionCollapsesWithoutHidingQuarryWalls");
    EditorPanel panel(LevelEditor::Tool::Tiles);
    const auto expanded = panel.frame();
    const auto terrainIconCount = [](const EditorPanel::Frame& frame) {
        return std::ranges::count_if(frame.icons, [](const EditorPanel::Icon& icon) {
            return !iconIsInPopup(icon) && editorTilePalette::isTerrain(icon.tile);
        });
    };
    CHECK(terrainIconCount(expanded) == 2);
    CHECK(expanded.text.find("Terrain") < expanded.text.find("Randomize Ground"));
    CHECK(expanded.text.find("Randomize Ground") < expanded.text.find("Randomize Walls"));
    CHECK(expanded.text.find("Randomize Walls") < expanded.text.find("\nPaint"));
    panel.clickPaletteControl("Terrain");
    const auto collapsed = panel.frame();
    CHECK(terrainIconCount(collapsed) == 0);
    CHECK(collapsed.text.find("Randomize Ground") == std::string::npos);
    CHECK(collapsed.text.find("Randomize Walls") == std::string::npos);
    CHECK(std::ranges::any_of(collapsed.icons, [](const EditorPanel::Icon& icon) {
        return !iconIsInPopup(icon) && icon.tile == TileType::Wall;
    }));
    CHECK(panel.shows("Paint Blend Mask"));
    panel.clickIcon(TileType::Wall);
    const auto quarryPicker = panel.frame();
    CHECK(static_cast<std::size_t>(std::ranges::count_if(quarryPicker.icons, [](const EditorPanel::Icon& icon) {
        return iconIsInPopup(icon) && tileTypeIsWall(icon.tile) && !tileTypeIsCliffWall(icon.tile);
    })) == wallStoneVariantCount);
    panel.clickIcon(TileType::WallStone08, true);
    CHECK(panel.editor.selectedTile() == TileType::WallStone08);
    CHECK(terrainIconCount(panel.frame()) == 0);
    panel.clickPaletteControl("Terrain");
    CHECK(terrainIconCount(panel.frame()) == 2);
}

void terrainPickersKeepEveryStyleAndRecentBrushes()
{
    TEST("terrainPickersKeepEveryStyleAndRecentBrushes");
    EditorPanel panel(LevelEditor::Tool::Tiles);
    for (const TileType family : { TileType::Ground, TileType::CliffWall }) {
        const auto* group = editorTilePalette::groupFor(family);
        CHECK(group != nullptr);
        if (!group) continue;
        const bool ground = family == TileType::Ground;
        CHECK(group->variants().size() == (ground ? groundRockVariantCount : cliffWallVariantCount));
        for (const TileType variant : group->variants()) {
            const auto selected = panel.editor.selectedTile();
            const auto displayed = group->contains(selected) ? selected : family;
            panel.clickIcon(displayed);
            const auto picker = panel.frame();
            CHECK(static_cast<std::size_t>(std::ranges::count_if(picker.icons, [&](const EditorPanel::Icon& icon) {
                return iconIsInPopup(icon) && group->contains(icon.tile);
            })) == group->variants().size());
            panel.clickIcon(variant, true);
            CHECK(panel.editor.selectedTile() == variant);
            CHECK(panel.editor.tool() == LevelEditor::Tool::Tiles);
            CHECK(std::ranges::find(panel.editor.recentTiles(), variant) != panel.editor.recentTiles().end());
        }
    }
    panel.clickPaletteControl("Terrain");
    panel.editor.setSelectedTile(TileType::WallStone03);
    const auto& recent = panel.editor.recentTiles();
    const auto groundRecent = std::ranges::find(recent, TileType::GroundRock10);
    CHECK(groundRecent != recent.end());
    if (groundRecent != recent.end()) {
        const auto slot = static_cast<std::size_t>(groundRecent - recent.begin());
        const std::string label = std::to_string(slot + 1) + " " +
            std::string(tileTypeName(*groundRecent)) + "##recent" + std::to_string(slot);
        panel.clickPaletteControl(label.c_str());
        CHECK(panel.editor.selectedTile() == TileType::GroundRock10);
    }
    CHECK(panel.editor.setCell({ 0, 0, 1 }, TileType::Air));
    CHECK(panel.editor.setCell({ 0, 0, 0 }, TileType::GroundRock09));
    CHECK(panel.editor.pickTile({ 0, 0, 0 }) == TileType::GroundRock09);
    CHECK(panel.editor.selectedTile() == TileType::GroundRock09);
    CHECK(!panel.shows("Randomize Ground"));
    panel.clickPaletteControl("Terrain");
    CHECK(std::ranges::any_of(panel.frame().icons, [](const EditorPanel::Icon& icon) {
        return !iconIsInPopup(icon) && icon.tile == TileType::GroundRock09;
    }));
}

void buttonPickerOffersFourPlacementsAndKeepsSelectedBrush()
{
    TEST("buttonPickerOffersFourPlacementsAndKeepsSelectedBrush");
    EditorPanel panel(LevelEditor::Tool::Tiles);
    const auto* group = editorTilePalette::groupFor(TileType::ButtonNorth);
    CHECK(group != nullptr);
    if (!group) return;
    CHECK(group->variants().size() == 4);
    for (const TileType variant : group->variants()) {
        const auto selected = panel.editor.selectedTile();
        panel.clickIcon(group->contains(selected) ? selected : TileType::ButtonNorth);
        const auto picker = panel.frame();
        CHECK(picker.text.find("Button direction") != std::string::npos);
        CHECK(std::ranges::count_if(picker.icons, [](const EditorPanel::Icon& icon) {
            return iconIsInPopup(icon) && tileTypeIsButton(icon.tile);
        }) == 4);
        panel.clickIcon(variant, true);
        CHECK(panel.editor.selectedTile() == variant);
        CHECK(std::ranges::find(panel.editor.recentTiles(), variant) != panel.editor.recentTiles().end());
        CHECK(std::ranges::count_if(panel.frame().icons, [variant](const EditorPanel::Icon& icon) {
            return !iconIsInPopup(icon) && tileTypeIsButton(icon.tile) && icon.tile == variant;
        }) == 1);
    }
}

void leverPickerOffersFourPlacementsAndKeepsSelectedBrush()
{
    TEST("leverPickerOffersFourPlacementsAndKeepsSelectedBrush");
    EditorPanel panel(LevelEditor::Tool::Tiles);
    const auto* group = editorTilePalette::groupFor(TileType::LeverNorth);
    CHECK(group != nullptr);
    if (!group) return;
    CHECK(group->variants().size() == 4);
    for (const TileType variant : group->variants()) {
        const auto selected = panel.editor.selectedTile();
        panel.clickIcon(group->contains(selected) ? selected : TileType::LeverNorth);
        const auto picker = panel.frame();
        CHECK(picker.text.find("Lever direction") != std::string::npos);
        CHECK(std::ranges::count_if(picker.icons, [](const EditorPanel::Icon& icon) {
            return iconIsInPopup(icon) && tileTypeIsLever(icon.tile);
        }) == 4);
        panel.clickIcon(variant, true);
        CHECK(panel.editor.selectedTile() == variant);
        CHECK(std::ranges::find(panel.editor.recentTiles(), variant) != panel.editor.recentTiles().end());
        CHECK(std::ranges::count_if(panel.frame().icons, [variant](const EditorPanel::Icon& icon) {
            return !iconIsInPopup(icon) && tileTypeIsLever(icon.tile) && icon.tile == variant;
        }) == 1);
    }
    CHECK(panel.shows("A lever stays on or off until activated again."));
}

void terrainRandomizeButtonsKeepAssignmentsAndUndo()
{
    TEST("terrainRandomizeButtonsKeepAssignmentsAndUndo");
    EditorPanel panel(LevelEditor::Tool::Tiles);
    panel.editor.resizeDocument(20, 6, false);
    for (int x = 0; x < 20; ++x) {
        CHECK(panel.editor.setCell({ x, 0, 0 }, TileType::GroundRock03));
        CHECK(panel.editor.setCell({ x, 1, 0 }, TileType::CliffWall02));
        CHECK(panel.editor.setCell({ x, 2, 0 }, TileType::WallStone03));
    }
    panel.click("Add Splat Map");
    CHECK(panel.editor.paintGroundSplat({ 3, 0, 0 }));
    CHECK(panel.editor.paintGroundSplat({ 3, 1, 0 }));
    panel.editor.setSelectedTile(TileType::GroundRock09);
    panel.editor.setActiveLayer(1);
    const auto before = panel.editor.documentDefinition();
    panel.clickPaletteControl("Randomize Ground");
    const auto groundRandomized = panel.editor.documentDefinition();
    CHECK(groundRandomized.layers != before.layers);
    CHECK(groundRandomized.layers[0][1] == before.layers[0][1]);
    CHECK(groundRandomized.layers[0][2] == before.layers[0][2]);
    CHECK(groundRandomized.groundSplats == before.groundSplats);
    CHECK(groundRandomized.groundPaint == before.groundPaint);
    CHECK(panel.editor.selectedTile() == TileType::GroundRock09);
    CHECK(panel.editor.activeLayer() == 1);
    CHECK(panel.editor.tryUndoEdit());
    CHECK(panel.editor.documentDefinition() == before);
    CHECK(panel.editor.tryRedoEdit());
    CHECK(panel.editor.documentDefinition() == groundRandomized);

    panel.clickPaletteControl("Randomize Walls");
    const auto wallsRandomized = panel.editor.documentDefinition();
    CHECK(wallsRandomized.layers != groundRandomized.layers);
    CHECK(wallsRandomized.layers[0][0] == groundRandomized.layers[0][0]);
    CHECK(wallsRandomized.groundSplats == before.groundSplats);
    CHECK(wallsRandomized.groundPaint == before.groundPaint);
    for (int x = 0; x < 20; ++x) {
        const auto cliff = charToTileType(wallsRandomized.layers[0][1][static_cast<std::size_t>(x)]);
        const auto stone = charToTileType(wallsRandomized.layers[0][2][static_cast<std::size_t>(x)]);
        CHECK(cliff && tileTypeIsCliffWall(*cliff));
        CHECK(stone && tileTypeIsWall(*stone) && !tileTypeIsCliffWall(*stone));
    }
    CHECK(panel.editor.selectedTile() == TileType::GroundRock09);
    CHECK(panel.editor.activeLayer() == 1);
    CHECK(panel.editor.tryUndoEdit());
    CHECK(panel.editor.documentDefinition() == groundRandomized);
    CHECK(panel.editor.tryRedoEdit());
    CHECK(panel.editor.documentDefinition() == wallsRandomized);
}

} // namespace
#endif

int main()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    try {
        addSplatKeepsMaskPaintingAvailable();
        newMaskActionUsesSelectedSplat();
        terrainSectionCollapsesWithoutHidingQuarryWalls();
        terrainPickersKeepEveryStyleAndRecentBrushes();
        buttonPickerOffersFourPlacementsAndKeepsSelectedBrush();
        leverPickerOffersFourPlacementsAndKeepsSelectedBrush();
        terrainRandomizeButtonsKeepAssignmentsAndUndo();
    } catch (const std::exception& error) {
        std::cerr << "Editor ImGui regression failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " ImGui editor checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
#else
    std::cout << "Editor UI is disabled in this build\n";
    return 77;
#endif
}

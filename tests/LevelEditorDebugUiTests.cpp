// Exercise the editor's actual ImGui buttons with mouse events. No native
// window or GPU is needed, and all editor files stay in a temporary project.
#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/LevelEditorDebugUi.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace sokoban;

class EditorPanel {
public:
    EditorPanel()
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
        editor.setTool(LevelEditor::Tool::Selectors);
        if (!editor.addGroundSplat({
                .name = "Ground", .base = "GroundGrass", .detail = "GroundRock",
                .mask = "InitialMask", .color = { 0.2f, 0.8f, 0.3f } })) {
            throw std::runtime_error("Could not initialize splat UI fixture");
        }
        ui_.initialize(editor);
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
    };

    Frame frame(const char* locateButton = nullptr)
    {
        ImGui::SetCurrentContext(context_);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ 1500, 5900 }, ImGuiCond_Always);
        ImGui::Begin("Editor regression", nullptr,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove);
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (locateButton) {
            // Navigation records the real item bounds by ID, so tests need no
            // hard-coded pixel positions or special production-side hooks.
            context_->NavWindow = window;
            ImGui::SetNavID(ImGui::GetID(locateButton), ImGuiNavLayer_Main,
                context_->CurrentFocusScopeId, ImRect());
            context_->NavIdIsAlive = false;
        }
        ImGui::LogToBuffer();
        ui_.draw(editor, overworld_, painter, bindings_, callbacks_);
        Frame result { .text = context_->LogBuffer.c_str() };
        if (locateButton && context_->NavIdIsAlive) {
            const ImRect& rect = window->NavRectRel[ImGuiNavLayer_Main];
            const ImVec2 center = rect.GetCenter();
            result.buttonCenter = ImVec2(center.x + window->Pos.x, center.y + window->Pos.y);
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
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(located.buttonCenter->x, located.buttonCenter->y);
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

} // namespace
#endif

int main()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    try {
        addSplatKeepsMaskPaintingAvailable();
        newMaskActionUsesSelectedSplat();
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

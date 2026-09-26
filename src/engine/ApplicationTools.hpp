#pragma once

#include "engine/AnimationCatalogDebugUi.hpp"
#include "engine/AnimationCatalogEditor.hpp"
#include "engine/AnimationPreviewDebugUi.hpp"
#include "engine/ApplicationDebugUi.hpp"
#include "engine/AssetManifestDebugUi.hpp"
#include "engine/AssetManifestEditor.hpp"
#include "engine/DecorationGizmo.hpp"
#include "engine/DecorationMeshCatalog.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/LevelEditorDebugUi.hpp"
#include "engine/LogDebugUi.hpp"
#include "engine/OverworldMapEditor.hpp"
#include "engine/ProfilerDebugUi.hpp"
#include "engine/InputRouter.hpp"
#include "engine/ShaderHotReload.hpp"
#include "engine/SolutionStore.hpp"
#include "engine/SourceWatcher.hpp"
#include "engine/SplatPainter.hpp"
#include "engine/TuningDebugUi.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/ui/Ui.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sokoban {

// Owns the editor and debug-only state coordinated by Application. Keeping this
// bundle out of Application.hpp isolates runtime code from tool dependencies.
class ApplicationTools {
public:
    void initialize(
        const std::filesystem::path& sourceLevelRoot,
        const std::filesystem::path& sourceAssetRoot,
        const std::filesystem::path& runtimeAssetRoot,
        int currentLevel,
        int currentScreen,
        AssetManifest& manifest,
        AnimationCatalog& animations);

    [[nodiscard]] std::optional<DecorationGizmo::Geometry>
        decorationGizmoGeometry(
            const VulkanRenderer& renderer,
            const VulkanRenderer::PreparedFrame& frame) const;

    [[nodiscard]] bool openGroundPainting(
        const std::filesystem::path& sourceAssetRoot,
        const std::filesystem::path& runtimeAssetRoot,
        AssetManifest& manifest,
        VulkanRenderer& renderer);
    [[nodiscard]] bool createGroundSplatMap(
        const std::filesystem::path& sourceAssetRoot,
        const std::filesystem::path& runtimeAssetRoot,
        AssetManifest& manifest,
        VulkanRenderer& renderer);
    [[nodiscard]] std::optional<std::string> registerDecorationMesh(
        const std::filesystem::path& sourceAssetRoot,
        const std::filesystem::path& runtimeAssetRoot,
        const std::filesystem::path& relativePath,
        AssetManifest& manifest,
        VulkanRenderer& renderer);
    void pushPaintedSplatMap(VulkanRenderer& renderer);
    void updateEditorInteraction(
        const InputRouter::EditorInput& input,
        const VulkanRenderer::PreparedFrame* previousRenderFrame,
        VulkanRenderer& renderer,
        Vec2 windowSize,
        Vec2 pixelSize);
    void drawBrushPreview(
        const VulkanRenderer& renderer,
        const VulkanRenderer::PreparedFrame* frame) const;
    void drawDecorationGizmo(
        const VulkanRenderer& renderer,
        const VulkanRenderer::PreparedFrame* frame,
        Vec2 pointer,
        Vec2 windowSize,
        Vec2 pixelSize,
        bool pointerCaptured) const;
    void drawSelectorLabels(
        const VulkanRenderer& renderer,
        const VulkanRenderer::PreparedFrame* frame) const;
    void drawDraftExitConfirmation();
    // Leaves draft playback for the document view (the modal's Stop Testing
    // button, and F5 while a draft is playing).
    void stopDraftPlayback();
    // Shader hot reload: watches the source shaders this build compiled,
    // recompiles edits with the build's glslc and flags, publishes them into
    // the staged tree, and asks the renderer to rebuild its pipelines.
    void enableShaderHotReload(const std::filesystem::path& runtimeAssetRoot);
    // Between frames: polls sources (a few times a second) and hands any
    // published modules to the renderer.
    void serviceShaderHotReload(VulkanRenderer& renderer);
    void drawShaderHotReloadPanel(const VulkanRenderer& renderer);
    // Every frame: the F6 recompile shortcut and a compile-error notice that
    // stays up until a compile succeeds.
    void drawShaderHotReloadOverlay(const VulkanRenderer& renderer);
    // The workspace's Session menu.
    void drawSessionMenu();
    // A screen or draft was just solved: record it into solutions/ on a
    // worker thread (engine/SolutionStore.hpp). Jobs run one at a time, in
    // order; results go to the Log and the Solutions panel.
    void saveSolve(solution::SolveToStore solve);
    // Re-files recordings after level files change, e.g. a draft was saved.
    void requestSolutionReconcile();
    // Every frame: reports a finished job and starts the next one.
    void serviceSolutionStore();
    // Level Editor panel section: what the store did recently.
    void drawSolutionPanel();
    void drawManifestReloadStatus();
    [[nodiscard]] bool bakeTileThumbnails(
        VulkanRenderer& renderer,
        UiContext& ui,
        const AssetManifest& manifest,
        const PresentationSettings& settings,
        const AnimationCatalog& animations,
        const std::filesystem::path& sourceAssetRoot,
        const std::filesystem::path& runtimeAssetRoot,
        Vec2 viewportSize);

    ApplicationDebugUi applicationDebugUi;
    ProfilerDebugUi profilerDebugUi;
    LogDebugUi logDebugUi;
    TuningDebugUi tuningDebugUi { SOKOBAN_SOURCE_ROOT_DIR };
    AssetManifestEditor assetManifestEditor;
    AssetManifestDebugUi assetManifestDebugUi;
    LevelEditor levelEditor;
    OverworldMapEditor overworldMapEditor;
    DecorationMeshCatalog decorationMeshCatalog;
    LevelEditorDebugUi levelEditorDebugUi;
    AnimationPreviewDebugUi animationPreviewDebugUi;
    AnimationCatalogEditor animationCatalogEditor;
    AnimationCatalogDebugUi animationCatalogDebugUi;
    DecorationGizmo decorationGizmo;
    SplatPainter splatPainter;
    std::optional<GridPosition3> hoverCell;
    // The board cell under the pointer before any edit-target resolution;
    // Shift+F5 plays the draft with the hero moved here.
    std::optional<GridPosition3> pickedCell;
    std::optional<std::size_t> hoverDecoration;
    std::optional<Vec3> brushPoint;
    std::uint64_t uploadedSplatRevision = 0;
    bool draftExitConfirmationOpen = false;
    bool bakeThumbnailsRequested = false;
    // Persisted in the developer session file (DevSession.hpp).
    bool resumeOnLaunch = true;

private:
    // A held-button tile drag. Each board column is edited at most once per
    // stroke, so holding still does not stack tiles or erase down through
    // layers, and LevelEditor folds the whole stroke into one undo record.
    struct TileStroke {
        bool deleting = false;
        bool replaceLayer = false;
        GridPosition anchor;
        GridPosition last;
        std::set<std::pair<int, int>> visited;
        // The pointer left the board (or crossed a panel) since `last`; the
        // next cell starts fresh instead of drawing a line back to it.
        bool resumeWithoutLine = false;
        // Painting outside grew the board, which shifts every coordinate.
        // Also set by undo/redo/save mid-drag. Nothing more until release.
        bool blocked = false;
    };
    std::optional<TileStroke> tileStroke_;

    void beginTileStroke(
        const InputRouter::EditorInput& input,
        GridPosition3 target,
        bool deleting);
    void continueTileStroke(
        const InputRouter::EditorInput& input,
        GridPosition3 picked);
    // Closes the editor's undo stroke but keeps ignoring the held button
    // until it is released.
    void interruptTileStroke();
    void handleEditorShortcuts(const InputRouter::EditorInput& input);

public:
    // Source hot reload (DI-10). Application::serviceSourceWatcher drives it.
    SourceWatcher sourceWatcher;
    bool sourceWatcherConfigured = false;
    std::uint64_t lastSourcePollTicks = 0;
    // Shown under the Asset Manifest tab after a manifest file changes.
    std::string manifestReloadStatus;

private:
    void reportSolutionStatus(std::string line, bool warning);

    // A queued solve, or nullopt for a reconcile-only pass.
    std::deque<std::optional<solution::SolveToStore>> solutionJobs_;
    std::future<std::vector<solution::StoreChange>> solutionJob_;
    std::string solutionJobName_;
    std::size_t solutionJobInputs_ = 0;
    struct LastSolve {
        std::uint64_t digest = 0;
        std::vector<solution::Input> inputs;
        bool operator==(const LastSolve&) const = default;
    };
    std::optional<LastSolve> lastSolve_;
    std::vector<std::string> solutionStatus_;
    std::unique_ptr<ShaderHotReload> shaderHotReload_;
    std::string shaderReloadStatus_;
    std::string shaderReloadDiagnostics_;
    bool shaderCompileFailed_ = false;
    bool shaderWatchEnabled_ = true;
    std::uint64_t lastShaderPollTicks_ = 0;

    bool updateGroundPainting(
        const InputRouter::EditorInput& input,
        const VulkanRenderer::PreparedFrame& previousRenderFrame,
        Vec2 pointerPixels,
        VulkanRenderer& renderer);
    bool updateDecorationEditing(
        const InputRouter::EditorInput& input,
        const VulkanRenderer::PreparedFrame& previousRenderFrame,
        Vec2 pointerPixels,
        VulkanRenderer& renderer);
    void persistManifestTexture(
        const std::string& name,
        const std::string& relativePath);
};

} // namespace sokoban

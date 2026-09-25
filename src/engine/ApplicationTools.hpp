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
#include "engine/InputRouter.hpp"
#include "engine/ShaderHotReload.hpp"
#include "engine/SplatPainter.hpp"
#include "engine/TuningDebugUi.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/ui/Ui.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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
    std::optional<std::size_t> hoverDecoration;
    std::optional<Vec3> brushPoint;
    std::uint64_t uploadedSplatRevision = 0;
    bool draftExitConfirmationOpen = false;
    bool bakeThumbnailsRequested = false;
    // Persisted in the developer session file (DevSession.hpp).
    bool resumeOnLaunch = true;

private:
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

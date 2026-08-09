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
#include "engine/InputRouter.hpp"
#include "engine/SplatPainter.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/ui/Ui.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

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
    AssetManifestEditor assetManifestEditor;
    AssetManifestDebugUi assetManifestDebugUi;
    LevelEditor levelEditor;
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

private:
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
        const std::filesystem::path& runtimeAssetRoot,
        const std::string& name,
        const std::string& relativePath);
};

} // namespace sokoban

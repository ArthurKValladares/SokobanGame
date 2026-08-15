#include "engine/ApplicationTools.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/DecorationAssetRegistry.hpp"
#include "engine/EditorInteraction.hpp"
#include "engine/Log.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/TileTypes.hpp"
#include "engine/render/PngWriter.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <vector>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

namespace sokoban {

void ApplicationTools::initialize(
    const std::filesystem::path& sourceLevelRoot,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    int currentLevel,
    int currentScreen,
    AssetManifest& manifest,
    AnimationCatalog& animations)
{
    levelEditor.initialize(
        sourceLevelRoot,
        runtimeAssetRoot / "levels",
        currentLevel,
        currentScreen);
    levelEditorDebugUi.initialize(levelEditor);
    animationPreviewDebugUi.initialize(sourceAssetRoot);
    if (animationCatalogEditor.initialize(
            sourceAssetRoot / "animation_catalog.json",
            runtimeAssetRoot / "animation_catalog.json",
            manifest)) {
        animations = animationCatalogEditor.catalog();
    }
    assetManifestEditor.initialize(sourceAssetRoot / "manifest.json");
    (void)decorationMeshCatalog.refresh(sourceAssetRoot, manifest);
}

std::optional<DecorationGizmo::Geometry>
ApplicationTools::decorationGizmoGeometry(
    const VulkanRenderer& renderer,
    const VulkanRenderer::PreparedFrame& frame) const
{
    const Level::Decoration* decoration = levelEditor.selectedDecoration();
    if (!decoration || levelEditor.tool() != LevelEditor::Tool::Decorations) {
        return std::nullopt;
    }
    return EditorInteraction::decorationGizmoGeometry(
        *decoration,
        [&](Vec3 world) {
            return renderer.projectToPixels(frame, world);
        });
}

bool ApplicationTools::openGroundPainting(
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    AssetManifest& manifest,
    VulkanRenderer& renderer)
{
    // Painting targets the editor document, so make that document visible
    // whenever a session is opened from a tool callback.
    levelEditor.setEditingDocument(true);

    const bool opened = splatPainter.open(
        {
            .documentPath = levelEditor.loadedDocumentPath(),
            .boardTilesWide = levelEditor.documentWidth(),
            .boardTilesHigh = levelEditor.documentHeight(),
            .sourceAssetRoot = sourceAssetRoot,
            .runtimeAssetRoot = runtimeAssetRoot,
        },
        manifest);
    if (opened) {
        RenderAssetRequirements requirements;
        requirements.requireTexture(splatPainter.texture());
        renderer.ensureAssets(requirements);
        uploadedSplatRevision = splatPainter.revision();
    }
    return opened;
}

bool ApplicationTools::createGroundSplatMap(
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    AssetManifest& manifest,
    VulkanRenderer& renderer)
{
    const std::optional<LevelLocation> location =
        levelLocationFromScreenPath(levelEditor.loadedDocumentPath());
    if (!location) {
        log::warning(log::Category::Assets)
            << "Ground painting needs a saved screen; save the document as "
               "levels/level<N>/screen<M>.scr first.";
        return false;
    }

    const CreatedSplatMap created = createBlankSplatMap(
        *location,
        levelEditor.documentWidth(),
        levelEditor.documentHeight(),
        sourceAssetRoot,
        runtimeAssetRoot);
    log::info(log::Category::Assets) << created.message;
    if (!created.created) {
        return false;
    }

    const std::string textureName =
        groundSplatMapTextureNameForScreen(*location);
    if (manifest.findTextureIdByName(textureName).isNone()) {
        const RenderTexture added = manifest.addTexture({
            .name = textureName,
            .path = created.relativePath,
            .tiling = false,
            .filter = TextureFilter::Linear,
            .colorSpace = TextureColorSpace::Linear,
        });
        if (added.isNone()) {
            log::error(log::Category::Assets)
                << "Could not register " << textureName
                << "; the texture descriptor array is full (max "
                << maxModelTextures << ").";
            return false;
        }
        renderer.syncManifestTextures();
        persistManifestTexture(
            runtimeAssetRoot, textureName, created.relativePath);
    }

    return openGroundPainting(
        sourceAssetRoot, runtimeAssetRoot, manifest, renderer);
}

void ApplicationTools::persistManifestTexture(
    const std::filesystem::path& runtimeAssetRoot,
    const std::string& name,
    const std::string& relativePath)
{
    const AssetManifest::Texture entry {
        .name = name,
        .path = relativePath,
        .tiling = false,
        .filter = TextureFilter::Linear,
        .colorSpace = TextureColorSpace::Linear,
    };

    assetManifestEditor.addTexture();
    assetManifestEditor.updateTexture(
        assetManifestEditor.textures().size() - 1, entry);
    if (!assetManifestEditor.save()) {
        log::error(log::Category::Assets)
            << "Could not write " << name << " to the source manifest: "
            << assetManifestEditor.status();
        return;
    }

    try {
        atomicFile::write(
            runtimeAssetRoot / "manifest.json",
            assetManifestEditor.serialize());
    } catch (const std::exception& error) {
        log::warning(log::Category::Assets)
            << "Saved " << name << " to the source manifest but could not "
            << "update the staged copy: " << error.what();
    }
}

std::optional<std::string> ApplicationTools::registerDecorationMesh(
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    const std::filesystem::path& relativePath,
    AssetManifest& manifest,
    VulkanRenderer& renderer)
{
    const DecorationAssetRegistry::Result result =
        DecorationAssetRegistry::registerMesh({
            .sourceAssetRoot = sourceAssetRoot,
            .runtimeAssetRoot = runtimeAssetRoot,
            .relativeMeshPath = relativePath,
            .runtimeManifest = manifest,
            .manifestEditor = assetManifestEditor,
        });
    if (!result.succeeded) {
        log::error(log::Category::Assets) << result.status;
        return std::nullopt;
    }

    renderer.syncManifestTextures();
    renderer.syncManifestModels();
    (void)decorationMeshCatalog.refresh(sourceAssetRoot, manifest);
    log::info(log::Category::Assets) << result.status;
    return result.modelName;
}

void ApplicationTools::pushPaintedSplatMap(VulkanRenderer& renderer)
{
    if (!splatPainter.active() ||
        splatPainter.revision() == uploadedSplatRevision) {
        return;
    }
    uploadedSplatRevision = splatPainter.revision();
    try {
        (void)renderer.updateTexture(
            splatPainter.texture(), splatPainter.canvas().toImage());
    } catch (const std::exception& error) {
        log::error(log::Category::Assets)
            << "Could not upload the painted splat map: " << error.what();
    }
}

void ApplicationTools::drawBrushPreview(
    const VulkanRenderer& renderer,
    const VulkanRenderer::PreparedFrame* frame) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!splatPainter.active() || !brushPoint || !frame) {
        return;
    }
    const SplatCanvas::Brush& brush = splatPainter.brush();
    const EditorInteraction::BrushPreview preview =
        EditorInteraction::brushPreview(
            brush,
            *brushPoint,
            [&](Vec3 world) {
                return renderer.projectToPixels(*frame, world);
            });
    if (preview.vertices.empty()) {
        return;
    }

    const bool white = brush.color == SplatCanvas::BrushColor::White;
    const ImU32 tint = white
        ? IM_COL32(255, 255, 255, 0)
        : IM_COL32(15, 15, 15, 0);
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const std::size_t indexLimit =
        static_cast<std::size_t>(std::numeric_limits<ImDrawIdx>::max());
    if (static_cast<std::size_t>(drawList->_VtxCurrentIdx) +
            preview.vertices.size() > indexLimit) {
        return;
    }

    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const auto base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
    drawList->PrimReserve(
        static_cast<int>(preview.indices.size()),
        static_cast<int>(preview.vertices.size()));
    for (const EditorInteraction::BrushVertex& vertex : preview.vertices) {
        const auto alpha = static_cast<ImU32>(std::clamp(
            std::lround(vertex.opacity * 255.0f), 0L, 255L));
        const ImU32 color = (tint & ~IM_COL32_A_MASK) |
            (alpha << IM_COL32_A_SHIFT);
        drawList->PrimWriteVtx(
            ImVec2(vertex.position.x, vertex.position.y), uv, color);
    }
    for (std::uint32_t index : preview.indices) {
        drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + index));
    }

    std::vector<ImVec2> rim;
    rim.reserve(preview.rim.size());
    for (Vec2 point : preview.rim) {
        rim.emplace_back(point.x, point.y);
    }
    drawList->AddPolyline(
        rim.data(),
        static_cast<int>(rim.size()),
        white ? IM_COL32(255, 255, 255, 130) : IM_COL32(0, 0, 0, 150),
        ImDrawFlags_Closed,
        1.5f);
#else
    (void)renderer;
    (void)frame;
#endif
}

void ApplicationTools::drawDecorationGizmo(
    const VulkanRenderer& renderer,
    const VulkanRenderer::PreparedFrame* frame,
    Vec2 pointer,
    Vec2 windowSize,
    Vec2 pixelSize,
    bool pointerCaptured) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!frame || !levelEditor.editingDocument()) {
        return;
    }
    const std::optional<DecorationGizmo::Geometry> geometry =
        decorationGizmoGeometry(renderer, *frame);
    if (!geometry) {
        return;
    }

    const Vec2 pointerAtPixels = EditorInteraction::pointerPixels(
        pointer, windowSize, pixelSize);
    const std::optional<DecorationGizmo::Axis> hovered = pointerCaptured
        ? std::nullopt
        : decorationGizmo.hoveredAxis(*geometry, pointerAtPixels);
    const std::optional<DecorationGizmo::Axis> active =
        decorationGizmo.activeAxis();
    constexpr std::array<ImU32, 3> axisColors {
        IM_COL32(235, 75, 72, 255),
        IM_COL32(80, 210, 105, 255),
        IM_COL32(72, 135, 245, 255),
    };
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const auto point = [](Vec2 value) { return ImVec2(value.x, value.y); };
    const auto colorFor = [&](std::size_t axis) {
        const DecorationGizmo::Axis value =
            static_cast<DecorationGizmo::Axis>(axis);
        return active == value || hovered == value
            ? IM_COL32(255, 226, 92, 255)
            : axisColors[axis];
    };

    if (decorationGizmo.mode() == DecorationGizmo::Mode::Rotate) {
        for (std::size_t axis = 0; axis < geometry->rings.size(); ++axis) {
            const std::vector<Vec2>& ring = geometry->rings[axis];
            for (std::size_t index = 1; index < ring.size(); ++index) {
                drawList->AddLine(
                    point(ring[index - 1]), point(ring[index]),
                    IM_COL32(20, 24, 30, 210), 5.5f);
                drawList->AddLine(
                    point(ring[index - 1]), point(ring[index]),
                    colorFor(axis), 2.8f);
            }
        }
        return;
    }

    for (std::size_t axis = 0; axis < geometry->axes.size(); ++axis) {
        const DecorationGizmo::AxisHandle& handle = geometry->axes[axis];
        const Vec2 delta {
            handle.end.x - handle.start.x,
            handle.end.y - handle.start.y,
        };
        const float magnitude = std::max(
            std::sqrt(delta.x * delta.x + delta.y * delta.y), 1.0f);
        const Vec2 direction { delta.x / magnitude, delta.y / magnitude };
        const Vec2 perpendicular { -direction.y, direction.x };
        const ImU32 color = colorFor(axis);
        drawList->AddLine(
            point(handle.start), point(handle.end),
            IM_COL32(20, 24, 30, 220), 6.5f);
        drawList->AddLine(
            point(handle.start), point(handle.end), color, 3.5f);
        if (decorationGizmo.mode() == DecorationGizmo::Mode::Translate) {
            const Vec2 base {
                handle.end.x - direction.x * 13.0f,
                handle.end.y - direction.y * 13.0f,
            };
            drawList->AddTriangleFilled(
                point(handle.end),
                point({ base.x + perpendicular.x * 6.0f,
                        base.y + perpendicular.y * 6.0f }),
                point({ base.x - perpendicular.x * 6.0f,
                        base.y - perpendicular.y * 6.0f }),
                color);
        } else {
            drawList->AddRectFilled(
                ImVec2(handle.end.x - 5.5f, handle.end.y - 5.5f),
                ImVec2(handle.end.x + 5.5f, handle.end.y + 5.5f),
                color,
                1.0f);
        }
    }
#else
    (void)renderer;
    (void)frame;
    (void)pointer;
    (void)windowSize;
    (void)pixelSize;
    (void)pointerCaptured;
#endif
}

void ApplicationTools::drawSelectorLabels(
    const VulkanRenderer& renderer,
    const VulkanRenderer::PreparedFrame* frame) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!frame || !levelEditor.editingDocument() ||
        !levelEditor.editingOverworld()) {
        return;
    }
    const std::vector<LevelEditor::LevelDirectory> levels =
        levelEditor.collectLevelDirectories();
    const std::vector<EditorInteraction::SelectorLabel> labels =
        EditorInteraction::selectorLabels(
            levelEditor.selectors(),
            [&](Vec3 world) {
                return renderer.projectToPixels(*frame, world);
            },
            [&](const Level::ScreenSelector& selector) {
                return "Selector " + std::to_string(selector.id) + ": " +
                    LevelEditor::selectorTargetLabel(selector, levels);
            });
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    for (const EditorInteraction::SelectorLabel& label : labels) {
        const ImVec2 size = ImGui::CalcTextSize(label.text.c_str());
        const ImVec2 position {
            label.anchor.x - size.x * 0.5f,
            label.anchor.y - size.y * 0.5f,
        };
        drawList->AddRectFilled(
            ImVec2(position.x - 4.0f, position.y - 2.0f),
            ImVec2(position.x + size.x + 4.0f, position.y + size.y + 2.0f),
            IM_COL32(16, 20, 26, 205),
            3.0f);
        drawList->AddText(position, IM_COL32(245, 247, 250, 255),
            label.text.c_str());
    }
#else
    (void)renderer;
    (void)frame;
#endif
}

void ApplicationTools::drawDraftExitConfirmation()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr const char* popupName = "Stop Testing Draft?";
    if (draftExitConfirmationOpen) {
        ImGui::OpenPopup(popupName);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            popupName,
            &draftExitConfirmationOpen,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Stop testing this draft and return to the editor?");
        ImGui::Separator();
        if (ImGui::Button("Stop Testing", ImVec2(120.0f, 0.0f))) {
            levelEditor.setEditingDocument(true);
            hoverCell.reset();
            draftExitConfirmationOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            draftExitConfirmationOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

bool ApplicationTools::bakeTileThumbnails(
    VulkanRenderer& renderer,
    UiContext& ui,
    const AssetManifest& manifest,
    const PresentationSettings& settings,
    const AnimationCatalog& animations,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    Vec2 viewportSize)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    namespace bake = tileThumbnails;
    RenderAssetRequirements requirements;
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        requirements.requireModel(manifest.modelForTile(definition.type));
    }
    requirements.requireTexture(
        manifest.findTextureIdByName(groundSplatBaseTextureName));
    requirements.requireTexture(
        manifest.findTextureIdByName(groundSplatDetailTextureName));
    requirements.requireTexture(
        manifest.findTextureIdByName(groundSplatMapTextureName));
    renderer.ensureAssets(requirements);

    bool allSucceeded = true;
    int baked = 0;
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!bake::shouldBake(definition.type)) {
            continue;
        }
        try {
            for (int warmup = 0; warmup < 2; ++warmup) {
                SDL_PumpEvents();
                ui.beginFrame(viewportSize, {}, false, false);
                renderer.beginDebugUiFrame();
                renderer.drawFrame(
                    renderer.prepareFrame(bake::buildBakeFrame(
                        definition.type,
                        manifest,
                        settings,
                        &animations)),
                    ui.drawData());
            }

            const VkExtent2D extent = renderer.renderExtent();
            const bake::CropRect crop = bake::cropFor(
                bake::buildBakeFrame(
                    definition.type, manifest, settings, &animations),
                extent.width,
                extent.height);
            const ImageData captured = renderer.captureRenderedFrame(
                VkRect2D {
                    .offset = { crop.x, crop.y },
                    .extent = { crop.width, crop.height },
                });
            const std::string relative = bake::assetPathFor(definition.type);
            std::vector<uint8_t> pixels(captured.rgba.size());
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                pixels[i] = static_cast<uint8_t>(captured.rgba[i]);
            }
            for (const std::filesystem::path& root :
                 { sourceAssetRoot, runtimeAssetRoot }) {
                const std::filesystem::path file = root / relative;
                std::error_code error;
                std::filesystem::create_directories(
                    file.parent_path(), error);
                writeRgbaPng(
                    file, captured.width, captured.height, pixels);
            }
            ++baked;
            log::info(log::Category::Assets)
                << "Baked " << relative << " (" << captured.width << "x"
                << captured.height << ")";
        } catch (const std::exception& error) {
            allSucceeded = false;
            log::error(log::Category::Assets)
                << "Could not bake a thumbnail for "
                << tileTypeName(definition.type) << ": " << error.what();
        }
    }

    log::info(log::Category::Assets)
        << "Baked " << baked << " tile thumbnail(s) into "
        << (sourceAssetRoot / "custom/thumbnails").string();
    renderer.waitIdle();
    return allSucceeded;
#else
    (void)renderer;
    (void)ui;
    (void)manifest;
    (void)settings;
    (void)animations;
    (void)sourceAssetRoot;
    (void)runtimeAssetRoot;
    (void)viewportSize;
    return false;
#endif
}

void ApplicationTools::updateEditorInteraction(
    const InputRouter::EditorInput& input,
    const VulkanRenderer::PreparedFrame* previousRenderFrame,
    VulkanRenderer& renderer,
    Vec2 windowSize,
    Vec2 pixelSize)
{
    hoverCell.reset();
    hoverDecoration.reset();
    brushPoint.reset();
    if (!input.moving) {
        levelEditor.cancelMove();
    }
    if (input.undoPressed) {
        if (decorationGizmo.dragging()) {
            decorationGizmo.endDrag();
            (void)levelEditor.endSelectedDecorationTransform(false);
        }
        const bool undone = splatPainter.active()
            ? splatPainter.undo()
            : levelEditor.tryUndoEdit();
        (void)undone;
        pushPaintedSplatMap(renderer);
        return;
    }
    if (input.pointerCaptured) {
        splatPainter.endStroke();
        if (decorationGizmo.dragging() && !input.primaryDown) {
            decorationGizmo.endDrag();
            (void)levelEditor.endSelectedDecorationTransform();
        }
        return;
    }
    if (!previousRenderFrame) {
        return;
    }

    const uint32_t documentWidth = levelEditor.documentWidth();
    const uint32_t documentHeight = levelEditor.documentHeight();
    if (documentWidth == 0 || documentHeight == 0 ||
        windowSize.x <= 0.0f || windowSize.y <= 0.0f ||
        pixelSize.x <= 0.0f || pixelSize.y <= 0.0f ||
        previousRenderFrame->levelWidth != documentWidth ||
        previousRenderFrame->levelHeight != documentHeight) {
        return;
    }

    const Vec2 pointerPixels = EditorInteraction::pointerPixels(
        input.pointerPosition, windowSize, pixelSize);
    if (updateGroundPainting(
            input, *previousRenderFrame, pointerPixels, renderer)) {
        return;
    }
    if (levelEditor.tool() == LevelEditor::Tool::Decorations) {
        if (input.translateGizmoPressed) {
            decorationGizmo.setMode(DecorationGizmo::Mode::Translate);
        } else if (input.rotateGizmoPressed) {
            decorationGizmo.setMode(DecorationGizmo::Mode::Rotate);
        } else if (input.scaleGizmoPressed) {
            decorationGizmo.setMode(DecorationGizmo::Mode::Scale);
        }
        if (updateDecorationEditing(
                input, *previousRenderFrame, pointerPixels, renderer)) {
            return;
        }
    }
    if (const std::optional<GridPosition3> clicked =
            renderer.pickIsoGridCell(*previousRenderFrame, pointerPixels)) {
        GridPosition3 target = *clicked;
        const bool editingDecorations =
            levelEditor.tool() == LevelEditor::Tool::Decorations;
        const bool editingSelectors =
            levelEditor.tool() == LevelEditor::Tool::Selectors;
        const bool deleting = input.deleting && !editingDecorations;
        if (input.moving) {
            target = levelEditor.resolveMoveTarget(target);
        } else if (editingSelectors) {
            target = levelEditor.resolveSelectorTarget(target);
        } else {
            target = levelEditor.resolveEditTarget(
                target,
                deleting,
                input.replaceLayer && !editingDecorations);
        }

        hoverCell = target;
        if (input.primaryPressed) {
            if (input.moving) {
                if (levelEditor.pendingMove()) {
                    (void)levelEditor.moveObject(target);
                } else {
                    (void)levelEditor.beginMove(target);
                }
                return;
            }
            if (editingDecorations) {
                (void)levelEditor.placeDecoration(target);
            } else if (editingSelectors) {
                const auto found = std::ranges::find(
                    levelEditor.selectors(),
                    target,
                    &Level::ScreenSelector::cell);
                if (found != levelEditor.selectors().end()) {
                    (void)levelEditor.selectSelector(
                        static_cast<std::size_t>(std::distance(
                            levelEditor.selectors().begin(), found)));
                    if (deleting) {
                        (void)levelEditor.deleteSelectedSelector();
                    }
                } else if (!deleting) {
                    (void)levelEditor.placeSelector(target);
                }
            } else if (deleting) {
                levelEditor.eraseCell(target);
            } else {
                levelEditor.paintCell(target);
            }
        }
    } else if (levelEditor.tool() == LevelEditor::Tool::Decorations &&
               input.primaryPressed) {
        levelEditor.clearDecorationSelection();
    } else if (levelEditor.tool() == LevelEditor::Tool::Selectors &&
               input.primaryPressed) {
        levelEditor.clearSelectorSelection();
    }
}

bool ApplicationTools::updateDecorationEditing(
    const InputRouter::EditorInput& input,
    const VulkanRenderer::PreparedFrame& previousRenderFrame,
    Vec2 pointerPixels,
    VulkanRenderer& renderer)
{
    hoverDecoration = renderer.pickDecoration(
        previousRenderFrame, pointerPixels);

    if (decorationGizmo.dragging()) {
        if (!input.primaryDown) {
            decorationGizmo.endDrag();
            (void)levelEditor.endSelectedDecorationTransform();
            return true;
        }
        if (const std::optional<Level::Decoration> transformed =
                decorationGizmo.updateDrag(pointerPixels)) {
            (void)levelEditor.previewSelectedDecorationTransform(*transformed);
        }
        return true;
    }

    if (!input.primaryPressed) {
        return false;
    }
    if (const std::optional<DecorationGizmo::Geometry> geometry =
            decorationGizmoGeometry(renderer, previousRenderFrame)) {
        const Level::Decoration* selected = levelEditor.selectedDecoration();
        if (selected && decorationGizmo.beginDrag(
                *geometry, pointerPixels, *selected)) {
            if (!levelEditor.beginSelectedDecorationTransform()) {
                decorationGizmo.endDrag();
            }
            return true;
        }
    }
    if (hoverDecoration) {
        (void)levelEditor.selectDecoration(*hoverDecoration);
        return true;
    }
    return false;
}

bool ApplicationTools::updateGroundPainting(
    const InputRouter::EditorInput& input,
    const VulkanRenderer::PreparedFrame& previousRenderFrame,
    Vec2 pointerPixels,
    VulkanRenderer& renderer)
{
    if (!splatPainter.active()) {
        return false;
    }
    if (levelLocationFromScreenPath(levelEditor.loadedDocumentPath()) !=
        splatPainter.location()) {
        splatPainter.close();
        return false;
    }
    if (splatPainter.followBoardResize(
            levelEditor.documentWidth(), levelEditor.documentHeight())) {
        pushPaintedSplatMap(renderer);
    }

    const std::optional<Vec3> groundPoint = renderer.pickIsoGroundPoint(
        previousRenderFrame, pointerPixels);
    brushPoint = groundPoint;

    if (!input.primaryDown) {
        splatPainter.endStroke();
        pushPaintedSplatMap(renderer);
        return true;
    }
    if (!groundPoint) {
        return true;
    }

    const Vec2 brushTile { groundPoint->x, groundPoint->y };
    if (splatPainter.strokeInProgress()) {
        (void)splatPainter.paintTo(brushTile);
    } else {
        (void)splatPainter.beginStroke(brushTile);
    }
    pushPaintedSplatMap(renderer);
    return true;
}

} // namespace sokoban

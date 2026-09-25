#include "engine/ApplicationTools.hpp"

#include "engine/ContentPipeline.hpp"
#include "engine/DecorationAssetRegistry.hpp"
#include "engine/EditorInteraction.hpp"
#include "engine/Log.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/TileTypes.hpp"
#include "engine/render/PngWriter.hpp"
#include "engine/render/ShaderCatalog.hpp"

#include "ShaderToolchain.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
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
        currentScreen,
        sourceAssetRoot / "manifest.json",
        runtimeAssetRoot / "manifest.json");
    overworldMapEditor.initialize(
        sourceLevelRoot,
        runtimeAssetRoot / "levels");
    levelEditorDebugUi.initialize(levelEditor);
    animationPreviewDebugUi.initialize(sourceAssetRoot);
    if (animationCatalogEditor.initialize(
            sourceAssetRoot / "animation_catalog.json",
            runtimeAssetRoot / "animation_catalog.json",
            manifest)) {
        animations = animationCatalogEditor.catalog();
    }
    assetManifestEditor.initialize(
        sourceAssetRoot / "manifest.json",
        runtimeAssetRoot / "manifest.json");
    (void)decorationMeshCatalog.refresh(sourceAssetRoot, manifest);
}

void ApplicationTools::enableShaderHotReload(
    const std::filesystem::path& runtimeAssetRoot)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(shaderToolchain::glslc, error)) {
        shaderReloadStatus_ = std::string("Unavailable: glslc not found at ") +
            shaderToolchain::glslc;
        log::warning(log::Category::Rendering)
            << "Shader hot reload is off. " << shaderReloadStatus_;
        return;
    }
    std::vector<std::string> modules;
    modules.reserve(shaderCatalog::sources.size());
    for (const std::string_view name : shaderCatalog::sources) {
        modules.emplace_back(name);
    }
    shaderHotReload_ = std::make_unique<ShaderHotReload>(
        ShaderHotReload::Config {
            .sourceDirectory = shaderToolchain::sourceDirectory,
            .includeDirectory = shaderToolchain::includeDirectory,
            .runtimeAssetRoot = runtimeAssetRoot,
            .moduleNames = std::move(modules),
            .compiler = glslcCompiler(
                shaderToolchain::glslc,
                std::vector<std::string>(
                    shaderToolchain::flags.begin(),
                    shaderToolchain::flags.end()),
                shaderToolchain::includeDirectory),
        });
    shaderReloadStatus_ = std::string("Watching ") +
        shaderToolchain::sourceDirectory;
}

void ApplicationTools::serviceShaderHotReload(VulkanRenderer& renderer)
{
    if (!shaderHotReload_) {
        return;
    }
    // Sampling seventeen sources and a handful of includes is cheap, but
    // there is no reason to stat them every frame.
    const std::uint64_t now = SDL_GetTicks();
    if (!shaderWatchEnabled_ ||
        (now - lastShaderPollTicks_ < 250 && !shaderHotReload_->compiling())) {
        return;
    }
    lastShaderPollTicks_ = now;
    shaderHotReload_->poll();
    std::optional<ShaderHotReload::Result> result =
        shaderHotReload_->takeResult();
    if (!result) {
        if (shaderHotReload_->compiling()) {
            shaderReloadStatus_ = "Compiling...";
        }
        return;
    }

    std::string names;
    for (const std::string& module : result->modules) {
        names += (names.empty() ? "" : ", ") + module;
    }
    shaderReloadDiagnostics_ = std::move(result->diagnostics);
    shaderCompileFailed_ = !result->published;
    if (result->published) {
        renderer.requestShaderReload();
        shaderReloadStatus_ = "Reloaded " + names;
        log::info(log::Category::Rendering)
            << "Shader hot reload: recompiled " << names;
        if (!shaderReloadDiagnostics_.empty()) {
            log::warning(log::Category::Rendering)
                << "glslc reported:\n" << shaderReloadDiagnostics_;
        }
    } else {
        shaderReloadStatus_ = "Compile failed; still running the last good "
            "shaders";
        log::error(log::Category::Rendering)
            << "Shader hot reload: compile failed for " << names << "\n"
            << shaderReloadDiagnostics_;
    }
}

void ApplicationTools::drawShaderHotReloadPanel(const VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::TextWrapped("%s", shaderReloadStatus_.c_str());
    if (!renderer.shaderReloadError().empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
            "Pipelines were not rebuilt: %s",
            renderer.shaderReloadError().c_str());
    }
    if (!shaderHotReload_) {
        return;
    }
    ImGui::Checkbox("Recompile shaders when they are saved", &shaderWatchEnabled_);
    if (ImGui::Button("Recompile All (F6)")) {
        shaderHotReload_->requestFullRecompile();
        lastShaderPollTicks_ = 0;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%zu modules; applied revision %llu",
        shaderHotReload_->modules().size(),
        static_cast<unsigned long long>(renderer.appliedShaderRevision()));
    if (!shaderReloadDiagnostics_.empty()) {
        ImGui::SeparatorText(
            shaderCompileFailed_ ? "Compiler errors" : "Compiler warnings");
        ImGui::InputTextMultiline(
            "##ShaderDiagnostics",
            shaderReloadDiagnostics_.data(),
            shaderReloadDiagnostics_.size() + 1,
            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 14.0f),
            ImGuiInputTextFlags_ReadOnly);
    }
#else
    (void)renderer;
#endif
}

void ApplicationTools::drawShaderHotReloadOverlay(const VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!shaderHotReload_) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) {
        shaderHotReload_->requestFullRecompile();
        lastShaderPollTicks_ = 0;
    }
    if (!shaderCompileFailed_ && renderer.shaderReloadError().empty()) {
        return;
    }
    // A failed compile is easy to miss while looking at the game, which
    // keeps drawing the previous shaders. Say so where the game is.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f,
            viewport->WorkPos.y + 12.0f),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##ShaderCompileFailed", nullptr, flags)) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
            shaderCompileFailed_ ? "Shader compile failed"
                                 : "Shader pipelines failed to rebuild");
        const std::string& detail = shaderCompileFailed_
            ? shaderReloadDiagnostics_
            : renderer.shaderReloadError();
        // The first few lines carry the file, line, and message.
        std::size_t end = 0;
        for (int line = 0; line < 4 && end < detail.size(); ++line) {
            const std::size_t newline = detail.find('\n', end);
            end = newline == std::string::npos ? detail.size() : newline + 1;
        }
        ImGui::TextUnformatted(detail.data(), detail.data() + end);
        ImGui::TextDisabled("Still drawing the last good shaders. "
                            "Details in the Shaders tab.");
    }
    ImGui::End();
#else
    (void)renderer;
#endif
}

void ApplicationTools::drawSessionMenu()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::MenuItem("Resume here on next launch", nullptr, &resumeOnLaunch);
    ImGui::TextDisabled(
        "Skips the title, continues the active save slot, and reopens the "
        "editor document.\nLaunch with --title to show the title once.");
#endif
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
            .textureName = levelEditor.overworldScreenId()
                ? std::optional<std::string> {
                      groundSplatMapTextureNameForOverworldScreen(
                          *levelEditor.overworldScreenId()) }
                : std::nullopt,
        },
        manifest);
    if (opened) {
        RenderAssetRequirements requirements;
        requirements.requireTexture(splatPainter.texture());
        renderer.waitForAssets(requirements);
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
    const std::optional<OverworldScreenId> overworldScreen =
        levelEditor.overworldScreenId();
    if (!location && !overworldScreen) {
        log::warning(log::Category::Assets)
            << "Ground painting needs a saved screen; save the document as "
               "a puzzle or composed-overworld screen first.";
        return false;
    }

    const CreatedSplatMap created = overworldScreen
        ? createBlankSplatMapAt(
              groundSplatMapAssetPathForOverworldScreen(*overworldScreen),
              levelEditor.documentWidth(),
              levelEditor.documentHeight(),
              sourceAssetRoot,
              runtimeAssetRoot)
        : createBlankSplatMap(
              *location,
              levelEditor.documentWidth(),
              levelEditor.documentHeight(),
              sourceAssetRoot,
              runtimeAssetRoot);
    log::info(log::Category::Assets) << created.message;
    if (!created.created) {
        return false;
    }

    const std::string textureName = overworldScreen
        ? groundSplatMapTextureNameForOverworldScreen(*overworldScreen)
        : groundSplatMapTextureNameForScreen(*location);
    if (manifest.findTextureIdByName(textureName).isNone()) {
        if (manifest.textures().size() >=
            renderer.textureDescriptorCapacity()) {
            log::error(log::Category::Assets)
                << "Could not register " << textureName
                << "; the runtime texture descriptor heap is full (capacity "
                << renderer.textureDescriptorCapacity() << ").";
            return false;
        }
        const RenderTexture added = manifest.addTexture({
            .name = textureName,
            .path = created.relativePath,
            .tiling = false,
            .filter = TextureFilter::Linear,
            .colorSpace = TextureColorSpace::Linear,
        });
        if (added.isNone()) {
            log::error(log::Category::Assets)
                << "Could not register " << textureName << ".";
            return false;
        }
        renderer.syncManifestTextures();
        persistManifestTexture(textureName, created.relativePath);
    }

    return openGroundPainting(
        sourceAssetRoot, runtimeAssetRoot, manifest, renderer);
}

void ApplicationTools::persistManifestTexture(
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
    try {
        if (renderer.updateTexture(
                splatPainter.texture(), splatPainter.canvas().toImage())) {
            uploadedSplatRevision = splatPainter.revision();
        }
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
    ImDrawList* drawList = renderer.hasGameViewportDisplay()
        ? ImGui::GetForegroundDrawList()
        : ImGui::GetBackgroundDrawList();
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
    constexpr std::array<DecorationGizmo::Axis, 3> gizmoAxes {
        DecorationGizmo::Axis::X,
        DecorationGizmo::Axis::Y,
        DecorationGizmo::Axis::Z,
    };
    constexpr std::array<ImU32, 3> axisColors {
        IM_COL32(235, 75, 72, 255),
        IM_COL32(80, 210, 105, 255),
        IM_COL32(72, 135, 245, 255),
    };
    ImDrawList* drawList = renderer.hasGameViewportDisplay()
        ? ImGui::GetForegroundDrawList()
        : ImGui::GetBackgroundDrawList();
    const auto point = [](Vec2 value) { return ImVec2(value.x, value.y); };
    const auto colorFor = [&](DecorationGizmo::Axis axis) {
        const std::size_t index = static_cast<std::size_t>(axis);
        return active == axis || hovered == axis
            ? IM_COL32(255, 226, 92, 255)
            : axisColors[index];
    };

    if (decorationGizmo.mode() == DecorationGizmo::Mode::Rotate) {
        for (const DecorationGizmo::Axis axis : gizmoAxes) {
            const std::size_t axisIndex = static_cast<std::size_t>(axis);
            const std::vector<Vec2>& ring = geometry->rings[axisIndex];
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

    for (const DecorationGizmo::Axis axis : gizmoAxes) {
        const std::size_t axisIndex = static_cast<std::size_t>(axis);
        const DecorationGizmo::AxisHandle& handle = geometry->axes[axisIndex];
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
    ImDrawList* drawList = renderer.hasGameViewportDisplay()
        ? ImGui::GetForegroundDrawList()
        : ImGui::GetBackgroundDrawList();
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
        if (ImGui::Button("Stop Testing", ImVec2(120.0f, 0.0f)) ||
            !levelEditor.playingDraft()) {
            // Also closes when F5 already stopped the draft underneath.
            if (levelEditor.playingDraft()) {
                stopDraftPlayback();
            }
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

void ApplicationTools::stopDraftPlayback()
{
    levelEditor.setEditingDocument(true);
    hoverCell.reset();
    pickedCell.reset();
    draftExitConfirmationOpen = false;
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
        if (tileTypeIsPlayerStart(definition.type)) {
            const CharacterType character = definition.type == TileType::Knight
                ? CharacterType::Knight
                : definition.type == TileType::Druid
                    ? CharacterType::Druid
                    : definition.type == TileType::Witch
                        ? CharacterType::Witch
                        : definition.type == TileType::Bard
                            ? CharacterType::Bard
                            : CharacterType::Rogue;
            requirements.requireModel(manifest.characterModel(character));
        } else {
            requirements.requireModel(manifest.modelForTile(definition.type));
        }
    }
    requirements.requireTexture(
        manifest.findTextureIdByName(groundSplatBaseTextureName));
    requirements.requireTexture(
        manifest.findTextureIdByName(groundSplatDetailTextureName));
    requirements.requireTexture(
        manifest.findTextureIdByName(groundSplatMapTextureName));
    renderer.waitForAssets(requirements);

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

    try {
        (void)refreshContentPackageIndex(runtimeAssetRoot);
    } catch (const std::exception& error) {
        allSucceeded = false;
        log::error(log::Category::Assets)
            << "Tile thumbnails were written, but the runtime content index "
            << "could not be refreshed: " << error.what();
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
    pickedCell.reset();
    hoverDecoration.reset();
    brushPoint.reset();
    if (tileStroke_ && !input.primaryDown) {
        (void)levelEditor.endStroke();
        tileStroke_.reset();
    }
    if (!input.moving) {
        levelEditor.cancelMove();
    }
    if (input.undoPressed || input.redoPressed) {
        if (decorationGizmo.dragging()) {
            decorationGizmo.endDrag();
            (void)levelEditor.endSelectedDecorationTransform(false);
        }
        interruptTileStroke();
        if (input.undoPressed) {
            (void)(splatPainter.active()
                    ? splatPainter.undo()
                    : levelEditor.tryUndoEdit());
        } else if (!splatPainter.active()) {
            // Ground painting keeps an undo stack only.
            (void)levelEditor.tryRedoEdit();
        }
        pushPaintedSplatMap(renderer);
        return;
    }
    handleEditorShortcuts(input);
    if (input.pointerCaptured) {
        if (tileStroke_) {
            tileStroke_->resumeWithoutLine = true;
        }
        splatPainter.endStroke();
        if (decorationGizmo.dragging() && !input.primaryDown) {
            decorationGizmo.endDrag();
            (void)levelEditor.endSelectedDecorationTransform();
        }
        return;
    }
    if (levelEditor.tool() == LevelEditor::Tool::Decorations &&
        input.secondaryPressed && levelEditor.selectedDecoration()) {
        if (decorationGizmo.dragging()) {
            decorationGizmo.endDrag();
            (void)levelEditor.endSelectedDecorationTransform(false);
        }
        levelEditor.clearDecorationSelection();
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
        pickedCell = *clicked;
        const bool tilePainting =
            !input.moving && !editingDecorations && !editingSelectors;
        if (tilePainting && input.pickModifier) {
            if (input.primaryPressed) {
                (void)levelEditor.pickTile(*clicked);
            }
            return;
        }
        if (tilePainting && tileStroke_ && !input.primaryPressed) {
            continueTileStroke(input, *clicked);
            return;
        }
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
            } else {
                beginTileStroke(input, target, deleting);
            }
        }
    } else if (tileStroke_) {
        tileStroke_->resumeWithoutLine = true;
    } else if (levelEditor.tool() == LevelEditor::Tool::Decorations &&
               input.primaryPressed) {
        levelEditor.clearDecorationSelection();
    } else if (levelEditor.tool() == LevelEditor::Tool::Selectors &&
               input.primaryPressed) {
        levelEditor.clearSelectorSelection();
    }
}

void ApplicationTools::handleEditorShortcuts(
    const InputRouter::EditorInput& input)
{
    if (input.savePressed) {
        interruptTileStroke();
        if (splatPainter.active()) {
            (void)splatPainter.save();
        } else if (levelEditor.saveLoadedDocument().sourceSaved()) {
            levelEditorDebugUi.syncDocumentPath(levelEditor);
        }
    }
    if (input.layerUpPressed) {
        levelEditor.stepActiveLayer(1);
    }
    if (input.layerDownPressed) {
        levelEditor.stepActiveLayer(-1);
    }
    if (input.cycleToolPressed) {
        if (decorationGizmo.dragging()) {
            decorationGizmo.endDrag();
            (void)levelEditor.endSelectedDecorationTransform(false);
        }
        interruptTileStroke();
        levelEditor.cycleTool();
    }
    if (input.recentTileSlot) {
        (void)levelEditor.selectRecentTile(*input.recentTileSlot);
    }
    if (input.toggleLayerLockPressed) {
        levelEditor.toggleLayerLock();
    }
}

void ApplicationTools::interruptTileStroke()
{
    if (tileStroke_) {
        (void)levelEditor.endStroke();
        tileStroke_->blocked = true;
    }
}

void ApplicationTools::beginTileStroke(
    const InputRouter::EditorInput& input,
    GridPosition3 target,
    bool deleting)
{
    (void)levelEditor.endStroke();
    (void)levelEditor.beginStroke();
    const uint32_t width = levelEditor.documentWidth();
    const uint32_t height = levelEditor.documentHeight();
    if (deleting) {
        (void)levelEditor.eraseCell(target);
    } else {
        (void)levelEditor.paintCell(target);
    }
    const GridPosition column { target.x, target.y };
    tileStroke_ = TileStroke {
        .deleting = deleting,
        .replaceLayer = input.replaceLayer,
        .anchor = column,
        .last = column,
    };
    tileStroke_->visited.emplace(column.x, column.y);
    if (levelEditor.documentWidth() != width ||
        levelEditor.documentHeight() != height) {
        interruptTileStroke();
    }
}

void ApplicationTools::continueTileStroke(
    const InputRouter::EditorInput& input,
    GridPosition3 picked)
{
    TileStroke& stroke = *tileStroke_;
    if (stroke.blocked) {
        return;
    }
    GridPosition current { picked.x, picked.y };
    if (input.lineConstraint) {
        current = EditorInteraction::constrainToAxis(stroke.anchor, current);
    }
    if (current == stroke.last && !stroke.resumeWithoutLine) {
        return;
    }
    const std::vector<GridPosition> columns = stroke.resumeWithoutLine
        ? std::vector<GridPosition> { current }
        : EditorInteraction::gridLine(stroke.last, current);
    stroke.resumeWithoutLine = false;
    stroke.last = current;

    const auto width = static_cast<int>(levelEditor.documentWidth());
    const auto height = static_cast<int>(levelEditor.documentHeight());
    for (const GridPosition column : columns) {
        // Only the first cell of a stroke may grow the board.
        if (column.x < 0 || column.y < 0 ||
            column.x >= width || column.y >= height ||
            !stroke.visited.emplace(column.x, column.y).second) {
            continue;
        }
        const GridPosition3 cell = levelEditor.resolveEditTarget(
            { column.x, column.y, picked.z },
            stroke.deleting,
            stroke.replaceLayer);
        if (stroke.deleting) {
            (void)levelEditor.eraseCell(cell);
        } else {
            (void)levelEditor.paintCell(cell);
        }
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
    if (levelEditor.loadedDocumentPath().lexically_normal() !=
        splatPainter.documentPath().lexically_normal()) {
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

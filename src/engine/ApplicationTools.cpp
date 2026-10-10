#include "engine/ApplicationTools.hpp"
#include "engine/ParticleConfig.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/DecorationAssetRegistry.hpp"
#include "engine/EditorCursorArt.hpp"
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
#include <chrono>
#include <cmath>
#include <exception>
#include <future>
#include <sstream>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

namespace sokoban {
namespace {

bool selectedDecorationMatchesTool(const LevelEditor& editor)
{
    const auto* selected = editor.selectedDecoration();
    return selected && editor.decorationToolActive() &&
        editor.decorationEditable(*selected) &&
        TileDecorations::isTileDecoration(selected->model) ==
            (editor.tool() == LevelEditor::Tool::TileDecorations);
}

} // namespace

bool ApplicationTools::matchingOverworldEditorRoot()
{
    const auto& topologyRoot = overworldMapEditor.projectLevelRoot();
    const auto& browserRoot = levelEditor.browserRoot();
    if (topologyRoot != neighborTopologyRoot_ || browserRoot != neighborBrowserRoot_) {
        neighborTopologyRoot_ = topologyRoot;
        neighborBrowserRoot_ = browserRoot;
        neighborRootsMatch_ = std::filesystem::absolute(topologyRoot).lexically_normal() ==
            std::filesystem::absolute(browserRoot).lexically_normal();
    }
    return neighborRootsMatch_;
}

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

Vec3 ApplicationTools::detachedCameraForward() const
{
    const float horizontal = std::cos(detachedCamera_.pitchRadians);
    return normalizeOr(
        Vec3 {
            horizontal * std::sin(detachedCamera_.yawRadians),
            horizontal * std::cos(detachedCamera_.yawRadians),
            std::sin(detachedCamera_.pitchRadians),
        },
        Vec3 { 0.0f, 1.0f, 0.0f });
}

void ApplicationTools::setDetachedCameraMouseCapture(
    SDL_Window* window,
    bool captured)
{
    if (detachedCamera_.mouseCaptured == captured) {
        return;
    }
    if (!SDL_SetWindowRelativeMouseMode(window, captured)) {
        log::warning(log::Category::Application)
            << "Could not " << (captured ? "capture" : "release")
            << " the detached-camera mouse: " << SDL_GetError();
        if (captured) {
            return;
        }
    }
    detachedCamera_.mouseCaptured = captured;
    detachedCamera_.pendingMouseDelta = {};
}

void ApplicationTools::drawDetachedCameraMenu(
    SDL_Window* window,
    const VulkanRenderer::PreparedFrame* frame)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const bool canEnable = frame && frame->cameraValid;
    if (ImGui::MenuItem(
            "Detached FPS Camera",
            nullptr,
            detachedCamera_.enabled,
            detachedCamera_.enabled || canEnable)) {
        if (detachedCamera_.enabled) {
            setDetachedCameraMouseCapture(window, false);
            detachedCamera_.enabled = false;
        } else if (frame) {
            const Vec3 forward = normalizeOr(
                frame->cameraForward,
                Vec3 { 0.0f, 1.0f, 0.0f });
            detachedCamera_.position = frame->cameraPosition;
            detachedCamera_.yawRadians = std::atan2(forward.x, forward.y);
            detachedCamera_.pitchRadians = std::asin(
                std::clamp(forward.z, -1.0f, 1.0f));
            detachedCamera_.homePosition = detachedCamera_.position;
            detachedCamera_.homeYawRadians = detachedCamera_.yawRadians;
            detachedCamera_.homePitchRadians = detachedCamera_.pitchRadians;
            detachedCamera_.verticalFovDegrees = std::clamp(
                frame->cameraVerticalFovDegrees,
                5.0f,
                120.0f);
            detachedCamera_.pendingMouseDelta = {};
            detachedCamera_.enabled = true;
        }
    }

    if (!detachedCamera_.enabled) {
        if (!canEnable) {
            ImGui::TextDisabled("A rendered 3D scene is required.");
        }
        return;
    }

    ImGui::Separator();
    ImGui::TextDisabled(
        detachedCamera_.mouseCaptured
            ? "Mouse captured - Escape releases it"
            : "Click the game viewport to capture the mouse");
    ImGui::TextDisabled("WASD move, Space/E up, Ctrl/Q down, Shift boosts");
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragFloat(
        "Move Speed",
        &detachedCamera_.moveSpeed,
        0.1f,
        0.1f,
        100.0f,
        "%.1f units/s",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragFloat(
        "Fast Multiplier",
        &detachedCamera_.fastMultiplier,
        0.1f,
        1.0f,
        20.0f,
        "%.1fx",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragFloat(
        "Look Sensitivity",
        &detachedCamera_.mouseSensitivityDegrees,
        0.01f,
        0.01f,
        2.0f,
        "%.2f deg/px",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat(
        "Vertical FOV",
        &detachedCamera_.verticalFovDegrees,
        5.0f,
        120.0f,
        "%.0f deg",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat3(
        "Position",
        &detachedCamera_.position.x,
        0.05f,
        0.0f,
        0.0f,
        "%.2f");
    if (ImGui::MenuItem("Reset Pose")) {
        detachedCamera_.position = detachedCamera_.homePosition;
        detachedCamera_.yawRadians = detachedCamera_.homeYawRadians;
        detachedCamera_.pitchRadians = detachedCamera_.homePitchRadians;
    }
#else
    (void)window;
    (void)frame;
#endif
}

bool ApplicationTools::handleDetachedCameraEvent(
    const SDL_Event& event,
    SDL_Window* window,
    bool gameViewportAvailable,
    bool pointerOwnedByDebugUi)
{
    if (!detachedCamera_.enabled) {
        return false;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        setDetachedCameraMouseCapture(window, false);
        return false;
    }
    if (detachedCamera_.mouseCaptured) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setDetachedCameraMouseCapture(window, false);
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            detachedCamera_.pendingMouseDelta += {
                event.motion.xrel,
                event.motion.yrel,
            };
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            return true;
        }
        return false;
    }
    if (gameViewportAvailable && !pointerOwnedByDebugUi &&
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
        setDetachedCameraMouseCapture(window, true);
        return detachedCamera_.mouseCaptured;
    }
    return false;
}

void ApplicationTools::updateDetachedCamera(
    float dt,
    const InputState& input)
{
    if (!detachedCamera_.enabled || !detachedCamera_.mouseCaptured) {
        detachedCamera_.pendingMouseDelta = {};
        return;
    }

    detachedCamera_.yawRadians -= degreesToRadians(
        detachedCamera_.pendingMouseDelta.x *
        detachedCamera_.mouseSensitivityDegrees);
    detachedCamera_.pitchRadians -= degreesToRadians(
        detachedCamera_.pendingMouseDelta.y *
        detachedCamera_.mouseSensitivityDegrees);
    detachedCamera_.pitchRadians = std::clamp(
        detachedCamera_.pitchRadians,
        degreesToRadians(-89.0f),
        degreesToRadians(89.0f));
    detachedCamera_.pendingMouseDelta = {};

    const Vec3 forward = detachedCameraForward();
    const Vec3 right = normalizeOr(
        cross(Vec3 { 0.0f, 0.0f, 1.0f }, forward),
        Vec3 { 1.0f, 0.0f, 0.0f });
    Vec3 movement;
    if (input.keyDown(SDL_SCANCODE_W)) {
        movement += forward;
    }
    if (input.keyDown(SDL_SCANCODE_S)) {
        movement -= forward;
    }
    if (input.keyDown(SDL_SCANCODE_D)) {
        movement += right;
    }
    if (input.keyDown(SDL_SCANCODE_A)) {
        movement -= right;
    }
    if (input.keyDown(SDL_SCANCODE_SPACE) ||
        input.keyDown(SDL_SCANCODE_E)) {
        movement.z += 1.0f;
    }
    if (input.keyDown(SDL_SCANCODE_LCTRL) ||
        input.keyDown(SDL_SCANCODE_RCTRL) ||
        input.keyDown(SDL_SCANCODE_Q)) {
        movement.z -= 1.0f;
    }
    if (lengthSquared(movement) <= normalizeEpsilonSquared) {
        return;
    }

    float speed = detachedCamera_.moveSpeed;
    if (input.keyDown(SDL_SCANCODE_LSHIFT) ||
        input.keyDown(SDL_SCANCODE_RSHIFT)) {
        speed *= detachedCamera_.fastMultiplier;
    }
    const float step = std::clamp(dt, 0.0f, 0.1f) * speed;
    detachedCamera_.position += normalize(movement) * step;
}

void ApplicationTools::applyDetachedCamera(RenderFrameData& frame) const
{
    if (!detachedCamera_.enabled ||
        frame.viewMode != RenderViewMode::Isometric3D) {
        return;
    }
    frame.cameraOverride = RenderFrameData::CameraOverride {
        .position = detachedCamera_.position,
        .forward = detachedCameraForward(),
        .verticalFovDegrees = detachedCamera_.verticalFovDegrees,
    };
}

void ApplicationTools::releaseDetachedCameraMouse(SDL_Window* window)
{
    setDetachedCameraMouseCapture(window, false);
}

void ApplicationTools::shutdownDetachedCamera(SDL_Window* window)
{
    setDetachedCameraMouseCapture(window, false);
    detachedCamera_.enabled = false;
}

bool ApplicationTools::detachedCameraActive() const
{
    return detachedCamera_.enabled;
}

bool ApplicationTools::detachedCameraCapturingMouse() const
{
    return detachedCamera_.enabled && detachedCamera_.mouseCaptured;
}

std::optional<DecorationGizmo::Geometry>
ApplicationTools::decorationGizmoGeometry(
    const VulkanRenderer& renderer,
    const VulkanRenderer::PreparedFrame& frame) const
{
    const Level::Decoration* decoration = levelEditor.selectedDecoration();
    if (!selectedDecorationMatchesTool(levelEditor)) {
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

    if (splatPainter.dirty() && !splatPainter.save()) return false;
    levelEditor.setGroundAssignmentPainting(false);
    levelEditor.showGroundAssignmentColors() = false;
    const Level::GroundSplat* selectedSplat = levelEditor.selectedGroundSplat();
    const bool opened = splatPainter.open(
        {
            .documentPath = levelEditor.loadedDocumentPath(),
            .boardTilesWide = levelEditor.documentWidth(),
            .boardTilesHigh = levelEditor.documentHeight(),
            .sourceAssetRoot = sourceAssetRoot,
            .runtimeAssetRoot = runtimeAssetRoot,
            .textureName = selectedSplat ? std::optional<std::string> { selectedSplat->mask }
                : levelEditor.overworldScreenId()
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

    if (splatPainter.dirty() && !splatPainter.save()) return false;
    const bool first = levelEditor.groundSplats().empty();
    const std::string prefix = overworldScreen
        ? groundSplatMapTextureNameForOverworldScreen(*overworldScreen)
        : groundSplatMapTextureNameForScreen(*location);
    std::string textureName = prefix;
    std::string relativePath = overworldScreen
        ? groundSplatMapAssetPathForOverworldScreen(*overworldScreen)
        : groundSplatMapAssetPathForScreen(*location);
    std::string mapName = "Ground";
    if (!first) {
        std::size_t suffix = 1;
        do {
            textureName = prefix + "_" + std::to_string(suffix);
            mapName = "Ground " + std::to_string(suffix + 1);
            ++suffix;
        } while (!manifest.findTextureIdByName(textureName).isNone() ||
            std::ranges::find(levelEditor.groundSplats(), mapName, &Level::GroundSplat::name) != levelEditor.groundSplats().end());
        relativePath = "custom/textures/" + textureName + ".png";
    }
    const CreatedSplatMap created = createBlankSplatMapAt(
        relativePath, levelEditor.documentWidth(), levelEditor.documentHeight(), sourceAssetRoot, runtimeAssetRoot);
    log::info(log::Category::Assets) << created.message;
    if (!created.created) return false;

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
        if (!persistManifestTexture(textureName, created.relativePath)) return false;
    }

    const Level::GroundSplat* selected = levelEditor.selectedGroundSplat();
    Level::GroundSplat splat {
        .name = mapName,
        .base = selected ? selected->base : std::string(groundSplatBaseTextureName),
        .detail = selected ? selected->detail : std::string(groundSplatDetailTextureName),
        .mask = textureName,
    };
    // Generate a distinct 8-bit assignment color; authors can change it later.
    for (uint32_t key = 0x40bf59; ; key = (key + 0x9e3779) & 0xffffff) {
        splat.color = { static_cast<float>((key >> 16) & 255) / 255.0f,
                        static_cast<float>((key >> 8) & 255) / 255.0f,
                        static_cast<float>(key & 255) / 255.0f };
        const auto same = [&](const Level::GroundSplat& existing) {
            return std::lround(existing.color.x * 255) == std::lround(splat.color.x * 255) &&
                std::lround(existing.color.y * 255) == std::lround(splat.color.y * 255) &&
                std::lround(existing.color.z * 255) == std::lround(splat.color.z * 255);
        };
        if (!std::ranges::any_of(levelEditor.groundSplats(), same)) break;
    }
    if (!levelEditor.addGroundSplat(std::move(splat))) return false;
    splatPainter.close();
    levelEditor.setGroundAssignmentPainting(true);
    return true;
}

bool ApplicationTools::createGroundBlendMask(
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    AssetManifest& manifest,
    VulkanRenderer& renderer)
{
    const Level::GroundSplat* selected = levelEditor.selectedGroundSplat();
    if (!selected) {
        log::warning(log::Category::Assets)
            << "Select or add a splat map before creating its blend mask.";
        return false;
    }
    const std::size_t selectedIndex =
        static_cast<std::size_t>(selected - levelEditor.groundSplats().data());
    Level::GroundSplat edited = *selected;
    if (splatPainter.dirty() && !splatPainter.save()) return false;
    if (manifest.textures().size() >= renderer.textureDescriptorCapacity()) {
        log::error(log::Category::Assets)
            << "Could not create a blend mask; the runtime texture descriptor "
               "heap is full (capacity "
            << renderer.textureDescriptorCapacity() << ").";
        return false;
    }

    const CreatedSplatMap created = createUniqueBlankSplatMap(
        edited.mask,
        levelEditor.documentWidth(),
        levelEditor.documentHeight(),
        sourceAssetRoot,
        runtimeAssetRoot,
        manifest);
    log::info(log::Category::Assets) << created.message;
    if (!created.created) return false;
    if (!persistManifestTexture(created.textureName, created.relativePath)) return false;
    const RenderTexture added = manifest.addTexture({
        .name = created.textureName,
        .path = created.relativePath,
        .tiling = false,
        .filter = TextureFilter::Linear,
        .colorSpace = TextureColorSpace::Linear,
    });
    if (added.isNone()) {
        log::error(log::Category::Assets)
            << "Could not register " << created.textureName << ".";
        return false;
    }
    renderer.syncManifestTextures();
    edited.mask = created.textureName;
    if (!levelEditor.updateGroundSplat(selectedIndex, std::move(edited))) return false;
    return openGroundPainting(sourceAssetRoot, runtimeAssetRoot, manifest, renderer);
}

bool ApplicationTools::persistManifestTexture(
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
        return false;
    }
    return true;
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
    const auto& levels = levelEditor.levelBrowserSnapshot();
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

void ApplicationTools::saveSolve(solution::SolveToStore solve)
{
    // Draft playback reports a solve again on every action that ends on the
    // Ends (cycling heroes, say); the run is the same, so store it once.
    LastSolve key {
        .digest = solution::levelDigest(solve.definition),
        .inputs = solve.inputs,
    };
    if (lastSolve_ == key) {
        return;
    }
    lastSolve_ = std::move(key);
    solutionJobs_.emplace_back(std::move(solve));
}

void ApplicationTools::requestSolutionReconcile()
{
    // One pending pass covers any number of level edits.
    if (solutionJobs_.empty() || solutionJobs_.back().has_value()) {
        solutionJobs_.emplace_back(std::nullopt);
    }
}

void ApplicationTools::reportSolutionStatus(
    std::string line, bool warning)
{
    if (warning) {
        log::warning(log::Category::Editor) << line;
    } else {
        log::info(log::Category::Editor) << line;
    }
    solutionStatus_.push_back(std::move(line));
    constexpr std::size_t kept = 6;
    if (solutionStatus_.size() > kept) {
        solutionStatus_.erase(
            solutionStatus_.begin(),
            solutionStatus_.end() - static_cast<std::ptrdiff_t>(kept));
    }
}

void ApplicationTools::serviceSolutionStore()
{
    if (solutionJob_.valid()) {
        if (solutionJob_.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            return;
        }
        const std::string name = std::move(solutionJobName_);
        const std::size_t inputs = solutionJobInputs_;
        try {
            for (const solution::StoreChange& change : solutionJob_.get()) {
                const std::string file = change.file.filename().string();
                std::string line;
                bool warning = false;
                using Kind = solution::StoreChange::Kind;
                switch (change.kind) {
                case Kind::Saved:
                    line += "Saved the ";
                    line += std::to_string(change.steps);
                    line += "-step solution of ";
                    line += name;
                    if (change.file.parent_path().filename() == "drafts") {
                        line += " as drafts/";
                        line += file;
                        line += " until the draft is saved.";
                    } else {
                        line += " as ";
                        line += file;
                        line += '.';
                    }
                    break;
                case Kind::KeptExisting:
                    line += "Solved ";
                    line += name;
                    line += " in ";
                    line += std::to_string(inputs);
                    line += " inputs; kept the stored ";
                    line += std::to_string(change.steps);
                    line += "-step ";
                    line += file;
                    line += '.';
                    break;
                case Kind::Moved:
                    line += "Moved a recorded solution ";
                    line += change.message;
                    line += " to ";
                    line += file;
                    line += " to match the levels on disk.";
                    break;
                case Kind::NotRecorded:
                    warning = true;
                    line += "Did not save the solve of ";
                    line += name;
                    line += ": ";
                    line += change.message;
                    line += ". Solutions replay one input at a time, so a "
                            "run that moved during a slide may not repeat.";
                    break;
                }
                reportSolutionStatus(std::move(line), warning);
            }
        } catch (const std::exception& error) {
            reportSolutionStatus(
                "Could not update solutions/: " + std::string(error.what()),
                true);
        }
    }
    if (solutionJobs_.empty()) {
        return;
    }
    std::optional<solution::SolveToStore> job =
        std::move(solutionJobs_.front());
    solutionJobs_.pop_front();
    solutionJobName_ = job ? job->name : std::string {};
    solutionJobInputs_ = job ? job->inputs.size() : 0;
    const std::filesystem::path root = SOKOBAN_SOURCE_ROOT_DIR;
    // Recording replays the whole solve, so it runs off the main thread. The
    // job owns copies of everything it reads.
    solutionJob_ = std::async(
        std::launch::async,
        [levels = root / "levels",
            solutions = root / "solutions",
            job = std::move(job)] {
            return solution::reconcileStore(levels, solutions, job);
        });
}

void ApplicationTools::drawSolutionPanel()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!ImGui::CollapsingHeader("Solutions")) {
        return;
    }
    ImGui::TextWrapped(
        "Every solve is recorded to solutions/ automatically, keeping the "
        "shortest run for each screen. The solution_replay test replays "
        "them after rule changes.");
    if (solutionJob_.valid() || !solutionJobs_.empty()) {
        ImGui::TextDisabled("Saving...");
    }
    for (const std::string& line : solutionStatus_) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", line.c_str());
    }
#endif
}

void ApplicationTools::drawManifestReloadStatus()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!manifestReloadStatus.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", manifestReloadStatus.c_str());
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
    bool allSucceeded = true;
    int baked = 0;
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!bake::shouldBake(definition.type)) {
            continue;
        }
        try {
            const RenderFrameData bakeFrame = bake::buildBakeFrame(
                definition.type, manifest, settings, &animations);
            // Load one picture at a time so the entire palette does not fill
            // the shared upload ring before rendering can consume it.
            renderer.waitForAssets(renderAssetRequirementsForFrame(bakeFrame));
            for (int warmup = 0; warmup < 2; ++warmup) {
                SDL_PumpEvents();
                ui.beginFrame(viewportSize, {}, false, false);
                renderer.beginDebugUiFrame();
                renderer.drawFrame(
                    renderer.prepareFrame(bakeFrame),
                    ui.drawData());
            }

            const VkExtent2D extent = renderer.renderExtent();
            const bake::CropRect crop = bake::cropFor(
                bakeFrame,
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

namespace {

SDL_Cursor* createEditorCursor(const editorCursorArt::Image& image)
{
    SDL_Surface* surface = SDL_CreateSurface(
        editorCursorArt::size, editorCursorArt::size, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        return nullptr;
    }
    const auto* source = image.rgba.data();
    auto* destination = static_cast<std::uint8_t*>(surface->pixels);
    const std::size_t rowBytes =
        static_cast<std::size_t>(editorCursorArt::size) * 4U;
    for (int row = 0; row < editorCursorArt::size; ++row) {
        std::copy_n(
            source + static_cast<std::size_t>(row) * rowBytes,
            rowBytes,
            destination + static_cast<std::size_t>(row) *
                static_cast<std::size_t>(surface->pitch));
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(
        surface, image.hotspotX, image.hotspotY);
    SDL_DestroySurface(surface);
    return cursor;
}

} // namespace

void ApplicationTools::updateEditorCursor(bool editorActive)
{
    const EditorCursor wanted =
        editorActive ? wantedEditorCursor_ : EditorCursor::Default;
    SDL_Cursor* cursor = nullptr;
    if (wanted == EditorCursor::Eyedropper) {
        if (eyedropperCursor_ == nullptr) {
            eyedropperCursor_ = createEditorCursor(editorCursorArt::eyedropper());
        }
        cursor = eyedropperCursor_;
    } else if (wanted == EditorCursor::Brush) {
        const Level::GroundSplat* ground = levelEditor.groundAssignmentPainting()
            ? levelEditor.selectedGroundSplat() : nullptr;
        const Vec3 paint = ground ? ground->color : levelEditor.activeLinkColor();
        if (brushCursor_ != nullptr &&
            !LevelEditor::sameLinkColor(paint, brushCursorColor_)) {
            if (shownEditorCursor_ == EditorCursor::Brush) {
                SDL_SetCursor(SDL_GetDefaultCursor());
                shownEditorCursor_ = EditorCursor::Default;
            }
            SDL_DestroyCursor(brushCursor_);
            brushCursor_ = nullptr;
        }
        if (brushCursor_ == nullptr) {
            brushCursor_ = createEditorCursor(editorCursorArt::brush(paint));
            brushCursorColor_ = paint;
        }
        cursor = brushCursor_;
    }
    if (wanted == shownEditorCursor_) {
        return;
    }
    // Dear ImGui's backend only sets the cursor when the one it wants
    // changes, and over the board it wants the arrow throughout, so it leaves
    // ours alone; leaving a tool cursor restores the arrow ourselves.
    if (cursor != nullptr) {
        SDL_SetCursor(cursor);
        shownEditorCursor_ = wanted;
    } else {
        SDL_SetCursor(SDL_GetDefaultCursor());
        shownEditorCursor_ = EditorCursor::Default;
    }
}

void ApplicationTools::shutdownEditorCursors()
{
    if (shownEditorCursor_ != EditorCursor::Default) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        shownEditorCursor_ = EditorCursor::Default;
    }
    if (eyedropperCursor_ != nullptr) {
        SDL_DestroyCursor(eyedropperCursor_);
        eyedropperCursor_ = nullptr;
    }
    if (brushCursor_ != nullptr) {
        SDL_DestroyCursor(brushCursor_);
        brushCursor_ = nullptr;
    }
}

void ApplicationTools::updateEditorInteraction(
    const InputRouter::EditorInput& input,
    const VulkanRenderer::PreparedFrame* previousRenderFrame,
    VulkanRenderer& renderer,
    Vec2 windowSize,
    Vec2 pixelSize)
{
    wantedEditorCursor_ = EditorCursor::Default;
    hoverCell.reset();
    pickedCell.reset();
    hoverDecoration.reset();
    brushPoint.reset();
    if (tileStroke_ && !input.primaryDown) {
        (void)levelEditor.endStroke();
        tileStroke_.reset();
    }
    if (linkColorStroke_ &&
        (!input.primaryDown || !input.paintLinkColorModifier)) {
        (void)levelEditor.endStroke();
        linkColorStroke_.reset();
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
        if (linkColorStroke_) {
            (void)levelEditor.endStroke();
            linkColorStroke_.reset();
        }
        groundAssignmentLast_.reset();
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
    // A tab switch can happen while a gizmo owns the pointer. Stop its
    // transform before the hidden decoration can consume another drag update.
    if (decorationGizmo.dragging() &&
        (!selectedDecorationMatchesTool(levelEditor) ||
         !levelEditor.transformingSelectedDecoration())) {
        decorationGizmo.endDrag();
        (void)levelEditor.endSelectedDecorationTransform(false);
    }
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
    // The eyedropper and the link-color brush take over the pointer while
    // their key is held, whatever the tool or other held keys: no placing,
    // deleting, moving, decoration or ground editing, and no preview of any
    // of those.
    if (input.pickModifier || input.paintLinkColorModifier) {
        updateEditorToolModifier(
            input, previousRenderFrame, renderer, windowSize, pixelSize);
        return;
    }
    if (levelEditor.decorationToolActive() &&
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
    if (levelEditor.decorationToolActive()) {
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
            levelEditor.decorationToolActive();
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
    } else if (levelEditor.decorationToolActive() &&
               input.primaryPressed) {
        levelEditor.clearDecorationSelection();
    } else if (levelEditor.tool() == LevelEditor::Tool::Selectors &&
               input.primaryPressed) {
        levelEditor.clearSelectorSelection();
    }
}

void ApplicationTools::updateEditorToolModifier(
    const InputRouter::EditorInput& input,
    const VulkanRenderer::PreparedFrame* previousRenderFrame,
    VulkanRenderer& renderer,
    Vec2 windowSize,
    Vec2 pixelSize)
{
    // The eyedropper wins when both keys are held.
    const bool eyedropper = input.pickModifier;
    wantedEditorCursor_ =
        eyedropper ? EditorCursor::Eyedropper : EditorCursor::Brush;

    // Whatever the pointer was doing stops here. A tile drag keeps ignoring
    // the held button until it is released, so letting go of the key
    // mid-drag does not start painting.
    interruptTileStroke();
    splatPainter.endStroke();
    if (decorationGizmo.dragging()) {
        decorationGizmo.endDrag();
        (void)levelEditor.endSelectedDecorationTransform();
    }
    if (eyedropper && linkColorStroke_) {
        (void)levelEditor.endStroke();
        linkColorStroke_.reset();
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
    const std::optional<GridPosition3> clicked =
        renderer.pickIsoGridCell(*previousRenderFrame, pointerPixels);
    if (!clicked) {
        return;
    }
    // hoverCell stays empty: neither tool shows a tile preview.
    pickedCell = *clicked;
    if (eyedropper) {
        if (input.primaryPressed) {
            (void)levelEditor.pickTile(*clicked);
        }
        return;
    }
    const GridPosition column { clicked->x, clicked->y };
    if (input.primaryPressed) {
        (void)levelEditor.endStroke();
        (void)levelEditor.beginStroke();
        (void)levelEditor.paintLinkColorAt(*clicked);
        linkColorStroke_ = LinkColorStroke { .last = column };
    } else if (linkColorStroke_ && linkColorStroke_->last != column) {
        // Each tile once per drag; the drag is one undo step.
        (void)levelEditor.paintLinkColorAt(*clicked);
        linkColorStroke_->last = column;
    }
}

void ApplicationTools::handleEditorShortcuts(
    const InputRouter::EditorInput& input)
{
    if (input.savePressed) {
        interruptTileStroke();
        if (splatPainter.active()) {
            if (!splatPainter.save()) return;
        }
        if (levelEditor.saveLoadedDocument().sourceSaved()) {
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
    if (input.rotateTileDecorationPressed &&
        levelEditor.tool() == LevelEditor::Tool::TileDecorations &&
        levelEditor.placingDecoration()) {
        levelEditor.rotateTileDecorationBrush();
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
    if (hoverDecoration &&
        (*hoverDecoration >= levelEditor.decorations().size() ||
         !levelEditor.decorationEditable(levelEditor.decorations()[*hoverDecoration]))) {
        hoverDecoration.reset();
    }

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
    if (levelEditor.groundAssignmentPainting()) {
        wantedEditorCursor_ = EditorCursor::Brush;
        if (!input.primaryDown) {
            (void)levelEditor.endStroke();
            groundAssignmentLast_.reset();
            return true;
        }
        const auto cell = renderer.pickIsoGridCell(previousRenderFrame, pointerPixels);
        if (cell) {
            if (!levelEditor.strokeActive()) groundAssignmentLast_.reset();
            (void)levelEditor.beginStroke();
            if (groundAssignmentLast_ && groundAssignmentLast_->z == cell->z) {
                for (const GridPosition column : EditorInteraction::gridLine(
                         { groundAssignmentLast_->x, groundAssignmentLast_->y }, { cell->x, cell->y })) {
                    (void)levelEditor.paintGroundSplat({ column.x, column.y, cell->z });
                }
            } else {
                (void)levelEditor.paintGroundSplat(*cell);
            }
            groundAssignmentLast_ = cell;
        } else {
            groundAssignmentLast_.reset();
        }
        return true;
    }
    groundAssignmentLast_.reset();
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

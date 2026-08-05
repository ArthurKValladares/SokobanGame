#include "engine/ApplicationTools.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/DecorationAssetRegistry.hpp"
#include "engine/Log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <numbers>
#include <vector>

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
    const std::optional<Vec2> projectedOrigin =
        renderer.projectToPixels(frame, decoration->position);
    if (!projectedOrigin) {
        return std::nullopt;
    }

    constexpr float targetAxisLengthPixels = 92.0f;
    constexpr float ringRadiusScale = 0.72f;
    const std::array<Vec3, 3> axes {
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
    };
    const auto addScaled = [](Vec3 origin, Vec3 axis, float amount) {
        return Vec3 {
            origin.x + axis.x * amount,
            origin.y + axis.y * amount,
            origin.z + axis.z * amount,
        };
    };
    const auto pixelDistance = [](Vec2 left, Vec2 right) {
        const float x = left.x - right.x;
        const float y = left.y - right.y;
        return std::sqrt(x * x + y * y);
    };

    DecorationGizmo::Geometry geometry;
    geometry.origin = *projectedOrigin;
    std::array<float, 3> worldLengths {};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        const std::optional<Vec2> projectedUnit = renderer.projectToPixels(
            frame, addScaled(decoration->position, axes[axis], 1.0f));
        if (!projectedUnit) {
            return std::nullopt;
        }
        const float unitPixels = std::max(
            pixelDistance(*projectedUnit, *projectedOrigin), 1.0f);
        worldLengths[axis] = std::clamp(
            targetAxisLengthPixels / unitPixels, 0.05f, 100.0f);
        const std::optional<Vec2> endpoint = renderer.projectToPixels(
            frame,
            addScaled(
                decoration->position, axes[axis], worldLengths[axis]));
        if (!endpoint) {
            return std::nullopt;
        }
        geometry.axes[axis] = {
            .start = *projectedOrigin,
            .end = *endpoint,
            .worldLength = worldLengths[axis],
        };
    }

    constexpr int ringSegments = 64;
    const std::array<std::array<std::size_t, 2>, 3> ringAxes {
        std::array<std::size_t, 2> { 1, 2 },
        std::array<std::size_t, 2> { 0, 2 },
        std::array<std::size_t, 2> { 0, 1 },
    };
    for (std::size_t ring = 0; ring < geometry.rings.size(); ++ring) {
        std::vector<Vec2>& points = geometry.rings[ring];
        points.reserve(ringSegments + 1);
        for (int segment = 0; segment <= ringSegments; ++segment) {
            const float angle = static_cast<float>(segment) *
                2.0f * std::numbers::pi_v<float> /
                static_cast<float>(ringSegments);
            const std::size_t first = ringAxes[ring][0];
            const std::size_t second = ringAxes[ring][1];
            Vec3 world = addScaled(
                decoration->position,
                axes[first],
                std::cos(angle) * worldLengths[first] * ringRadiusScale);
            world = addScaled(
                world,
                axes[second],
                std::sin(angle) * worldLengths[second] * ringRadiusScale);
            const std::optional<Vec2> pixel =
                renderer.projectToPixels(frame, world);
            if (!pixel) {
                return std::nullopt;
            }
            points.push_back(*pixel);
        }
    }
    return geometry;
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

} // namespace sokoban

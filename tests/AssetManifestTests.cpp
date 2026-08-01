#include "engine/AssetManifest.hpp"
#include "engine/render/GltfMesh.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

using Json = nlohmann::json;

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

template <typename Fn>
void checkThrows(Fn&& fn, const char* label)
{
    try {
        fn();
        ++failures;
        std::cerr << "FAIL (no throw): " << label << '\n';
    } catch (const std::exception&) {
    }
}

std::optional<std::filesystem::path> assetsRootFromEnvironment()
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "SOKOBAN_ASSETS") != 0 || value == nullptr) {
        return std::nullopt;
    }
    const std::filesystem::path result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv("SOKOBAN_ASSETS");
    return value == nullptr
        ? std::nullopt
        : std::optional<std::filesystem::path>(value);
#endif
}

constexpr const char* validManifest = R"json(
{
  "format": 1,
  "textures": [
    { "name": "Unused", "path": "textures/unused.png" },
    { "name": "Tex", "path": "textures/tex.png" },
    { "name": "PrimitiveA", "path": "textures/a.png" },
    { "name": "Spacer", "path": "textures/spacer.png" },
    { "name": "PrimitiveB", "path": "textures/b.png" }
  ],
  "models": [
    {
      "name": "Box",
      "path": "models/box.gltf",
      "preserveSourceScale": true
    },
    {
      "name": "Hero",
      "path": "models/hero.glb",
      "geometry": "skinned",
      "material": { "mode": "texture", "texture": "Tex" },
      "attachments": [
        {
          "path": "models/sword.gltf",
          "node": "handslot.r",
          "rotateHalfTurn": true
        }
      ],
      "preserveAspectRatio": true,
      "rotateHalfTurn": true,
      "role": "player"
    },
    {
      "name": "Belt",
      "path": "models/belt.gltf",
      "material": {
        "mode": "primitive-materials",
        "slots": [
          { "texture": "PrimitiveA" },
          { "texture": "PrimitiveB", "scrollV": true }
        ]
      }
    }
  ],
  "animations": [
    { "name": "Idle", "path": "anims/idle.glb", "clip": 8, "role": "player-idle" },
    { "name": "Move", "path": "anims/move.glb", "clip": 7, "role": "player-move" },
    { "name": "Push", "path": "anims/push.glb", "clip": 1, "role": "player-push" },
    { "name": "Death", "path": "anims/death.glb", "clip": 3, "role": "player-death" },
    { "name": "DeadIdle", "path": "anims/death.glb", "clip": 4, "role": "player-dead-idle" }
  ],
  "tiles": [
    { "tile": "Wall", "model": "Box", "scale": 1.25 },
    { "tile": "Player", "model": "Hero", "scale": 1.1 },
    { "tile": "Ground", "scale": 0.9 }
  ],
  "sounds": [
    {
      "name": "footsteps",
      "volume": 0.3,
      "files": ["audio/step with spaces 1.ogg", "audio/step2.ogg"]
    }
  ],
  "music": [
    { "level": 0, "file": "audio/track zero.ogg" },
    { "level": 2, "file": "audio/track two.ogg", "volume": 0.8 }
  ]
}
)json";

void checkJsonThrows(const auto& mutate, const char* label)
{
    Json json = Json::parse(validManifest);
    mutate(json);
    checkThrows([&] { (void)sokoban::AssetManifest::parse(json.dump()); }, label);
}

void testValidManifest()
{
    using sokoban::AssetManifest;
    const AssetManifest manifest = AssetManifest::parse(validManifest);

    check(manifest.textures().size() == 5, "five textures");
    check(!manifest.textures()[0].tiling, "textures clamp unless marked tiling");
    check(manifest.textures()[0].filter == sokoban::TextureFilter::Nearest,
        "textures point sample unless asked for linear");
    check(manifest.textures()[0].colorSpace == sokoban::TextureColorSpace::Srgb,
        "textures are colour unless declared linear data");

    // Address mode, filtering, and colour space are independent: the splat
    // map wants clamped + linear + linear-data, which no single flag covers.
    const AssetManifest sampled = AssetManifest::parse(R"({
  "format": 1,
  "textures": [
    { "name": "Tex", "path": "t.png", "tiling": true, "filter": "linear" },
    { "name": "Data", "path": "d.png", "filter": "linear", "colorSpace": "linear" }
  ],
  "models": [ { "name": "Hero", "path": "h.glb", "geometry": "skinned", "role": "player" } ],
  "animations": [
    { "name": "Idle", "path": "a.glb", "role": "player-idle" },
    { "name": "Move", "path": "a.glb", "role": "player-move" },
    { "name": "Push", "path": "a.glb", "role": "player-push" },
    { "name": "Death", "path": "a.glb", "role": "player-death" },
    { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" }
  ]
})");
    check(sampled.textures()[0].tiling, "tiling flag parsed");
    check(sampled.textures()[0].filter == sokoban::TextureFilter::Linear,
        "linear filter parsed");
    check(sampled.textures()[0].colorSpace == sokoban::TextureColorSpace::Srgb,
        "tiling does not imply linear colour space");
    check(!sampled.textures()[1].tiling,
        "linear filtering does not imply repeat addressing");
    check(sampled.textures()[1].filter == sokoban::TextureFilter::Linear,
        "linear filter parsed on data texture");
    check(sampled.textures()[1].colorSpace == sokoban::TextureColorSpace::Linear,
        "linear colour space parsed");
    check(manifest.models().size() == 3, "three models");
    check(manifest.animations().size() == 5, "five animations");

    const sokoban::RenderModel box = manifest.modelIdByName("Box");
    check(manifest.model(box).preserveSourceScale,
        "box preserves authored source scale");

    const sokoban::RenderModel hero = manifest.modelIdByName("Hero");
    check(!hero.isCube(), "hero id valid");
    check(manifest.playerModel() == hero, "player role resolved");
    check(manifest.model(hero).geometry == sokoban::ModelGeometry::Skinned, "hero skinned");
    check(manifest.model(hero).preserveAspectRatio, "hero preserves aspect");
    check(manifest.model(hero).rotateHalfTurn, "hero rotates half turn");
    check(manifest.model(hero).materialMode == sokoban::ModelMaterialMode::SingleTexture,
        "hero single texture");
    check(manifest.model(hero).textureIndex == 1, "hero texture index resolved by name");
    check(manifest.model(hero).attachments.size() == 1,
        "hero attachment parsed");
    check(manifest.model(hero).attachments[0].path == "models/sword.gltf" &&
            manifest.model(hero).attachments[0].node == "handslot.r",
        "attachment path and node preserved");
    check(manifest.model(hero).attachments[0].rotateHalfTurn,
        "attachment local half turn parsed");

    const sokoban::RenderModel belt = manifest.modelIdByName("Belt");
    check(manifest.model(belt).hasScrollingMaterial(), "scrolling material flag");
    check(manifest.model(belt).materialMode == sokoban::ModelMaterialMode::PrimitiveMaterials,
        "belt primitive material");
    check(manifest.model(belt).primitiveMaterials.size() == 2,
        "belt material slot count");
    check(manifest.model(belt).primitiveMaterials[0].textureIndex == 2 &&
            manifest.model(belt).primitiveMaterials[1].textureIndex == 4,
        "each primitive texture resolved independently by name");
    check(!manifest.model(belt).primitiveMaterials[0].scrollV &&
            manifest.model(belt).primitiveMaterials[1].scrollV,
        "per-material behavior stays independent of descriptor index");

    check(manifest.playerIdleAnimation() == manifest.animationIdByName("Idle"), "idle role");
    check(manifest.playerMoveAnimation() == manifest.animationIdByName("Move"), "move role");
    check(manifest.playerPushAnimation() == manifest.animationIdByName("Push"), "push role");
    check(manifest.playerDeathAnimation() == manifest.animationIdByName("Death"), "death role");
    check(manifest.playerDeadIdleAnimation() == manifest.animationIdByName("DeadIdle"),
        "dead idle role");
    check(manifest.animation(manifest.playerDeathAnimation()).clip == 3, "death clip number");
    check(manifest.animation(manifest.playerDeadIdleAnimation()).clip == 4,
        "dead idle clip number");
    check(manifest.animation(manifest.playerIdleAnimation()).clip == 8, "idle clip");
    check(manifest.animation(manifest.playerIdleAnimation()).path == "anims/idle.glb",
        "animation path parsed");

    check(manifest.modelForTile(sokoban::TileType::Wall) == manifest.modelIdByName("Box"),
        "wall tile model");
    check(manifest.tileScale(sokoban::TileType::Wall) == 1.25f, "wall tile scale");
    check(manifest.tileEntries().size() == 3, "authored tile entries retained");
    check(manifest.modelForTile(sokoban::TileType::Ground).isCube(), "ground stays cube");
    check(manifest.tileScale(sokoban::TileType::Ground) == 0.9f, "ground scale without model");
    check(manifest.modelForTile(sokoban::TileType::End).isCube(), "unlisted tile defaults to cube");
    check(manifest.tileScale(sokoban::TileType::End) == 1.0f, "unlisted tile default scale");

    check(manifest.soundSet("footsteps").size() == 2, "footstep files");
    check(manifest.soundSet("footsteps")[0] == "audio/step with spaces 1.ogg",
        "sound path with spaces");
    check(manifest.soundSet("missing").empty(), "unknown sound set is empty");
    check(manifest.soundSetVolume("footsteps") == 0.3f, "sound set volume");
    check(manifest.soundSetVolume("missing") == 1.0f, "unknown sound set volume defaults to 1");

    check(manifest.musicForLevel(0) != nullptr && *manifest.musicForLevel(0) == "audio/track zero.ogg",
        "music level 0");
    check(manifest.musicForLevel(1) == nullptr, "no music for level 1");
    check(manifest.musicForLevel(2) != nullptr, "music level 2");
    check(manifest.musicTracks()[0].volume == 1.0f, "music volume defaults to 1");
    check(manifest.musicTracks()[1].volume == 0.8f, "music track volume parsed");
}

void testSyntaxAndSchemaFailures()
{
    using sokoban::AssetManifest;
    checkThrows([&] { (void)AssetManifest::parse("{]"); }, "malformed JSON");
    checkThrows([&] { (void)AssetManifest::parse("[]"); }, "root must be object");
    checkThrows([&] { (void)AssetManifest::parse("{}"); }, "format is required");
    checkThrows([&] { (void)AssetManifest::parse(R"({"format":2})"); }, "unsupported format");

    checkJsonThrows([](Json& json) { json["bogus"] = true; }, "unknown root property");
    checkJsonThrows([](Json& json) { json["textures"] = "wrong"; }, "array type enforced");
    // A typo in these must not silently fall back to the default sampling.
    checkJsonThrows([](Json& json) { json["textures"][0]["filter"] = "bilinear"; },
        "unknown texture filter");
    checkJsonThrows([](Json& json) { json["textures"][0]["colorSpace"] = "rec709"; },
        "unknown texture colour space");
    checkJsonThrows([](Json& json) { json["textures"][0]["path"] = 42; }, "string type enforced");
    checkJsonThrows([](Json& json) { json["models"][0]["mystery"] = true; },
        "unknown model property");
    checkJsonThrows([](Json& json) { json["models"][0]["preserveAspectRatio"] = 1; },
        "boolean type enforced");
    checkJsonThrows([](Json& json) { json["models"][0]["preserveSourceScale"] = 1; },
        "source scale boolean type enforced");
    checkJsonThrows([](Json& json) { json["animations"][0]["clip"] = -1; },
        "non-negative clip enforced");
    checkJsonThrows([](Json& json) { json["music"][0]["level"] = -1; },
        "non-negative level enforced");
    checkJsonThrows([](Json& json) {
        json["models"][0]["material"] = {
            { "mode", "none" }, { "texture", "Tex" },
        };
    }, "material mode fields enforced");
    checkJsonThrows([](Json& json) {
        json["models"][0]["material"] = {
            { "mode", "primitive-materials" },
        };
    }, "primitive material mappings required");
    checkJsonThrows([](Json& json) {
        json["models"][0]["material"] = {
            { "mode", "primitive-materials" },
            { "slots", Json::array() },
        };
    }, "primitive material mappings cannot be empty");
    checkJsonThrows([](Json& json) {
        json["models"][0]["attachments"] = {
            { { "path", "models/sword.gltf" }, { "node", "hand" } },
        };
    }, "static models cannot own skeleton attachments");
    checkJsonThrows([](Json& json) {
        json["models"][1]["attachments"] = {
            { { "path", "models/sword.gltf" } },
        };
    }, "attachment node is required");
}

void testDomainValidationFailures()
{
    checkJsonThrows([](Json& json) {
        json["tiles"].push_back({ { "tile", "Wall" }, { "model", "Missing" } });
    }, "unknown tile model");
    checkJsonThrows([](Json& json) {
        json["tiles"].push_back({ { "tile", "Bogus" }, { "scale", 2 } });
    }, "unknown tile name");
    checkJsonThrows([](Json& json) {
        json["tiles"].push_back({ { "tile", "Wall" }, { "scale", 2 } });
    }, "duplicate tile");
    checkJsonThrows([](Json& json) {
        json["models"].push_back({
            { "name", "Hero2" }, { "path", "p.glb" }, { "geometry", "skinned" },
            { "role", "player" },
        });
    }, "duplicate player role");
    checkJsonThrows([](Json& json) {
        json["models"].push_back({ { "name", "Box" }, { "path", "q.gltf" } });
    }, "duplicate model name");
    checkJsonThrows([](Json& json) {
        json["music"].push_back({ { "level", 0 }, { "file", "again.ogg" } });
    }, "duplicate music level");
    checkJsonThrows([](Json& json) {
        json["sounds"].push_back({ { "name", "empty-set" }, { "files", Json::array() } });
    }, "sound set without files");
    checkJsonThrows([](Json& json) { json["sounds"][0]["volume"] = -1; },
        "negative sound volume");
    checkJsonThrows([](Json& json) {
        json["models"].push_back({
            { "name", "NoTex" }, { "path", "p.gltf" },
            { "material", { { "mode", "texture" }, { "texture", "Ghost" } } },
        });
    }, "unknown material texture");
    checkJsonThrows([](Json& json) {
        json["models"].push_back({
            { "name", "BadPrimitiveTexture" }, { "path", "p.gltf" },
            { "material", {
                { "mode", "primitive-materials" },
                { "slots", {
                    { { "texture", "PrimitiveA" } },
                    { { "texture", "Ghost" } },
                } },
            } },
        });
    }, "unknown primitive texture base");
    checkJsonThrows([](Json& json) { json["animations"].erase(2); },
        "missing player-push role");
    checkJsonThrows([](Json& json) { json["animations"].erase(3); },
        "missing player-death role");
    checkJsonThrows([](Json& json) { json["animations"].erase(4); },
        "missing player-dead-idle role");
}

// A splat map is weight data covering the board once, so all three sampling
// options differ from a normal colour atlas. Getting any one wrong is visible
// but easy to misdiagnose: repeat echoes painted spots across the board,
// nearest makes brush edges blocky, and sRGB silently skews every weight.
bool splatMapSamplingIsCorrect(
    const sokoban::AssetManifest& manifest,
    std::string_view name)
{
    const sokoban::RenderTexture id = manifest.findTextureIdByName(name);
    if (id.isNone()) {
        return false;
    }
    const sokoban::AssetManifest::Texture& texture =
        manifest.textures()[id.index()];
    return !texture.tiling &&
        texture.filter == sokoban::TextureFilter::Linear &&
        texture.colorSpace == sokoban::TextureColorSpace::Linear;
}

void testRuntimeTextureRegistration()
{
    using sokoban::AssetManifest;
    AssetManifest manifest = AssetManifest::parse(validManifest);
    const std::size_t original = manifest.textures().size();

    // Appending yields a fresh id and leaves existing ids alone, which is what
    // makes runtime registration safe: ids are indices into this list.
    const sokoban::RenderTexture existing =
        manifest.findTextureIdByName(manifest.textures()[0].name);
    const sokoban::RenderTexture added = manifest.addTexture({
        .name = "GroundSplatMap7_2",
        .path = "custom/textures/ground_splat_level7_screen2.png",
        .filter = sokoban::TextureFilter::Linear,
        .colorSpace = sokoban::TextureColorSpace::Linear,
    });
    check(!added.isNone(), "runtime texture registered");
    check(manifest.textures().size() == original + 1, "texture list grew");
    check(manifest.findTextureIdByName("GroundSplatMap7_2") == added,
        "registered texture resolves by name");
    check(manifest.findTextureIdByName(manifest.textures()[0].name) == existing,
        "existing texture ids are undisturbed");
    check(manifest.textures()[added.index()].colorSpace ==
            sokoban::TextureColorSpace::Linear,
        "registered sampling options are kept");

    // The same rules parsing enforces, since these would otherwise surface as
    // a duplicate name or an out-of-bounds descriptor index at draw time.
    check(manifest.addTexture({ .name = "GroundSplatMap7_2", .path = "x.png" })
              .isNone(),
        "duplicate texture name rejected");
    check(manifest.addTexture({ .name = "", .path = "x.png" }).isNone(),
        "empty texture name rejected");
    check(manifest.addTexture({ .name = "NoPath", .path = "" }).isNone(),
        "empty texture path rejected");

    while (manifest.textures().size() < sokoban::maxModelTextures) {
        const sokoban::RenderTexture filler = manifest.addTexture({
            .name = "Filler" + std::to_string(manifest.textures().size()),
            .path = "filler.png",
        });
        check(!filler.isNone(), "filler texture registered");
    }
    check(manifest.textures().size() == sokoban::maxModelTextures,
        "texture list filled to the cap");
    check(manifest.addTexture({ .name = "OneTooMany", .path = "x.png" })
              .isNone(),
        "registration stops at the descriptor array cap");
}

void testRuntimeDecorationModelRegistration()
{
    using sokoban::AssetManifest;
    AssetManifest manifest = AssetManifest::parse(validManifest);
    const std::size_t original = manifest.models().size();
    const sokoban::RenderModel existing =
        manifest.modelIdByName(manifest.models().front().name);

    const sokoban::RenderModel added = manifest.addModel({
        .name = "Decoration_Tree",
        .path = "scenery/tree.gltf",
        .preserveSourceScale = true,
    });
    check(!added.isCube(), "runtime decoration model registered");
    check(manifest.models().size() == original + 1, "model list grew");
    check(manifest.modelIdByName("Decoration_Tree") == added,
        "registered model resolves by name");
    check(manifest.model(added).preserveSourceScale,
        "runtime decoration retains authored scale policy");
    check(manifest.modelIdByName(manifest.models().front().name) == existing,
        "existing model ids are undisturbed");
    check(manifest.addModel({
              .name = "Decoration_Tree",
              .path = "other.gltf",
          }).isCube(),
        "duplicate runtime model name rejected");
    check(manifest.addModel({ .name = "", .path = "x.gltf" }).isCube(),
        "empty runtime model name rejected");
    check(manifest.addModel({ .name = "NoPath", .path = "" }).isCube(),
        "empty runtime model path rejected");
}

void testDecorationMeshCanPreserveAuthoredScale()
{
    const std::optional<std::filesystem::path> root =
        assetsRootFromEnvironment();
    if (!root.has_value()) {
        return;
    }

    const std::filesystem::path desk =
        *root / "KayKit Furniture Bits 1.0/Assets/gltf/desk.gltf";
    const sokoban::MeshData normalized = sokoban::loadGltfMesh(desk);
    const sokoban::MeshData authored = sokoban::loadGltfMesh(
        desk,
        {
            .preserveSourceScale = true,
        });

    bool normalizedInsideUnitCube = true;
    for (const sokoban::MeshVertex& vertex : normalized.vertices) {
        normalizedInsideUnitCube =
            normalizedInsideUnitCube &&
            vertex.position.x >= -0.0001f && vertex.position.x <= 1.0001f &&
            vertex.position.y >= -0.0001f && vertex.position.y <= 1.0001f &&
            vertex.position.z >= -0.0001f && vertex.position.z <= 1.0001f;
    }
    bool authoredKeepsWideBounds = false;
    for (const sokoban::MeshVertex& vertex : authored.vertices) {
        authoredKeepsWideBounds =
            authoredKeepsWideBounds ||
            vertex.position.x < -1.0f || vertex.position.x > 1.0f;
    }
    check(normalizedInsideUnitCube,
        "default mesh loading still normalizes into one tile");
    check(authoredKeepsWideBounds,
        "decoration mesh loading retains authored dimensions");
}

void testRealManifestFile()
{
    const std::optional<std::filesystem::path> root = assetsRootFromEnvironment();
    if (!root.has_value()) {
        return;
    }
    using sokoban::AssetManifest;
    const AssetManifest manifest =
        AssetManifest::loadFromFile(*root / "manifest.json");
    check(!manifest.playerModel().isCube(), "real manifest has a player model");
    check(manifest.soundSet("footsteps").size() == 5, "real manifest footsteps");
    check(manifest.soundSet("stone-drag").size() == 4, "real manifest drags");
    check(manifest.soundSet("mirror-swap").size() == 1, "real manifest mirror swap");
    check(manifest.soundSet("mirror-swap")[0].ends_with("Woosh/woosh1.ogg"),
        "real manifest mirror swap uses woosh1");
    // The material layers are sampled in world-tile UVs that leave 0..1, so
    // they must repeat; without it the ground is smeared edge texels.
    for (std::string_view name : {
             sokoban::groundSplatBaseTextureName,
             sokoban::groundSplatDetailTextureName,
         }) {
        const sokoban::RenderTexture id = manifest.findTextureIdByName(name);
        check(!id.isNone(), "real manifest declares the ground material layer");
        const AssetManifest::Texture& texture = manifest.textures()[id.index()];
        check(texture.tiling, "real manifest ground material layer tiles");
        check(texture.filter == sokoban::TextureFilter::Linear,
            "real manifest ground material layer filters smoothly");
    }
    // Splat maps are the opposite: weight data spanning the board once. They
    // must NOT repeat (a painted spot would echo across the board) and must
    // NOT be sRGB (a painted 0.5 has to reach the shader as a 0.5 weight).
    check(splatMapSamplingIsCorrect(
              manifest, sokoban::groundSplatMapTextureName),
        "real manifest shared splat map samples as clamped linear data");
    // Dropping the per-screen maps from the manifest is otherwise silent:
    // every screen just falls back to the shared map and still renders, so
    // assert at least one survives.
    int screenSplatMaps = 0;
    for (int levelIndex = 0;; ++levelIndex) {
        int screensForLevel = 0;
        for (int screenIndex = 0;; ++screenIndex) {
            const sokoban::RenderTexture id = manifest.findTextureIdByName(
                sokoban::groundSplatMapTextureNameForScreen(
                    sokoban::LevelLocation {
                        .level = levelIndex,
                        .screen = screenIndex,
                    }));
            if (id.isNone()) {
                break;
            }
            const sokoban::LevelLocation location {
                .level = levelIndex,
                .screen = screenIndex,
            };
            check(
                splatMapSamplingIsCorrect(
                    manifest,
                    sokoban::groundSplatMapTextureNameForScreen(location)),
                "real manifest per-screen splat map samples as clamped linear data");
            // The editor builds this path when creating a map in-game, and the
            // generator builds the same name in Python. If they drift, the two
            // write different files for one screen.
            check(manifest.textures()[id.index()].path ==
                    sokoban::groundSplatMapAssetPathForScreen(location),
                "per-screen splat map path matches the shared convention");
            ++screensForLevel;
        }
        if (screensForLevel == 0) {
            break;
        }
        screenSplatMaps += screensForLevel;
    }
    check(screenSplatMaps > 0, "real manifest declares per-screen splat maps");
    check(!manifest.textureIdByName("Smoke01").isNone(),
        "real manifest has first mirror smoke texture");
    check(!manifest.textureIdByName("Smoke10").isNone(),
        "real manifest has last mirror smoke texture");
    check(manifest.musicForLevel(3) != nullptr, "real manifest level 3 music");
    check(!manifest.modelForTile(sokoban::TileType::Wall).isCube(), "real manifest wall model");
    check(manifest.modelForTile(sokoban::TileType::Decorative).isCube(),
        "real manifest decorative block defaults to procedural cube");

    const AssetManifest::Animation& death =
        manifest.animation(manifest.playerDeathAnimation());
    const uint32_t deathIndex =
        sokoban::animationIndexFromManifestClip(death.clip);
    const sokoban::GltfAnimationClip deathClip =
        sokoban::loadGltfAnimationClip(*root / death.path, deathIndex);
    check(
        deathClip.name == "Death_B",
        "real manifest death role resolves to Death_B");
    check(deathClip.durationSeconds > 0.0f, "real death clip has a duration");

    const AssetManifest::Animation& deadIdle =
        manifest.animation(manifest.playerDeadIdleAnimation());
    const uint32_t deadIdleIndex =
        sokoban::animationIndexFromManifestClip(deadIdle.clip);
    const sokoban::GltfAnimationClip deadIdleClip =
        sokoban::loadGltfAnimationClip(*root / deadIdle.path, deadIdleIndex);
    check(
        deadIdleClip.name == "Death_B_Pose",
        "real manifest dead idle role resolves to Death_B_Pose");
}

} // namespace

int main()
{
    testValidManifest();
    testSyntaxAndSchemaFailures();
    testDomainValidationFailures();
    testRuntimeTextureRegistration();
    testRuntimeDecorationModelRegistration();
    testDecorationMeshCanPreserveAuthoredScale();
    testRealManifestFile();

    if (failures != 0) {
        std::cerr << failures << " asset manifest checks failed\n";
        return 1;
    }
    std::cout << "All asset manifest checks passed\n";
    return 0;
}

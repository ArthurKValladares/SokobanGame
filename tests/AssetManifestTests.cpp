#include "engine/AssetManifest.hpp"

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
    { "name": "Tex", "path": "textures/tex.png" }
  ],
  "models": [
    { "name": "Box", "path": "models/box.gltf" },
    {
      "name": "Hero",
      "path": "models/hero.glb",
      "geometry": "skinned",
      "material": { "mode": "texture", "texture": "Tex" },
      "preserveAspectRatio": true,
      "rotateHalfTurn": true,
      "role": "player"
    },
    {
      "name": "Belt",
      "path": "models/belt.gltf",
      "material": { "mode": "primitive-texture-index", "index": 0 },
      "beltScroll": true
    }
  ],
  "animations": [
    { "name": "Idle", "path": "anims/idle.glb", "clip": 8, "role": "player-idle" },
    { "name": "Move", "path": "anims/move.glb", "clip": 7, "role": "player-move" },
    { "name": "Push", "path": "anims/push.glb", "clip": 1, "role": "player-push" }
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

    check(manifest.textures().size() == 1, "one texture");
    check(manifest.models().size() == 3, "three models");
    check(manifest.animations().size() == 3, "three animations");

    const sokoban::RenderModel hero = manifest.modelIdByName("Hero");
    check(!hero.isCube(), "hero id valid");
    check(manifest.playerModel() == hero, "player role resolved");
    check(manifest.model(hero).geometry == sokoban::ModelGeometry::Skinned, "hero skinned");
    check(manifest.model(hero).preserveAspectRatio, "hero preserves aspect");
    check(manifest.model(hero).rotateHalfTurn, "hero rotates half turn");
    check(manifest.model(hero).materialMode == sokoban::ModelMaterialMode::SingleTexture,
        "hero single texture");
    check(manifest.model(hero).textureIndex == 0, "hero texture index resolved");

    const sokoban::RenderModel belt = manifest.modelIdByName("Belt");
    check(manifest.model(belt).beltScroll, "belt scroll flag");
    check(manifest.model(belt).primitiveTextures, "primitive texture loading inferred");
    check(manifest.model(belt).materialMode == sokoban::ModelMaterialMode::PrimitiveTextureIndex,
        "belt primitive material");

    check(manifest.playerIdleAnimation() == manifest.animationIdByName("Idle"), "idle role");
    check(manifest.playerMoveAnimation() == manifest.animationIdByName("Move"), "move role");
    check(manifest.playerPushAnimation() == manifest.animationIdByName("Push"), "push role");
    check(manifest.animation(manifest.playerIdleAnimation()).clip == 8, "idle clip");
    check(manifest.animation(manifest.playerIdleAnimation()).path == "anims/idle.glb",
        "animation path parsed");

    check(manifest.modelForTile(sokoban::TileType::Wall) == manifest.modelIdByName("Box"),
        "wall tile model");
    check(manifest.tileScale(sokoban::TileType::Wall) == 1.25f, "wall tile scale");
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
    checkJsonThrows([](Json& json) { json["textures"][0]["path"] = 42; }, "string type enforced");
    checkJsonThrows([](Json& json) { json["models"][0]["mystery"] = true; },
        "unknown model property");
    checkJsonThrows([](Json& json) { json["models"][0]["preserveAspectRatio"] = 1; },
        "boolean type enforced");
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
            { "mode", "primitive-texture-index" },
        };
    }, "primitive material index required");
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
            { "name", "BadIdx" }, { "path", "p.gltf" },
            { "material", { { "mode", "primitive-texture-index" }, { "index", 9 } } },
        });
    }, "primitive texture index out of range");
    checkJsonThrows([](Json& json) { json["animations"].erase(2); },
        "missing player-push role");
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
    check(manifest.musicForLevel(3) != nullptr, "real manifest level 3 music");
    check(!manifest.modelForTile(sokoban::TileType::Wall).isCube(), "real manifest wall model");
}

} // namespace

int main()
{
    testValidManifest();
    testSyntaxAndSchemaFailures();
    testDomainValidationFailures();
    testRealManifestFile();

    if (failures != 0) {
        std::cerr << failures << " asset manifest checks failed\n";
        return 1;
    }
    std::cout << "All asset manifest checks passed\n";
    return 0;
}

#include "engine/AssetManifest.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

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

constexpr const char* validManifest = R"(
# comment
texture Tex
  path textures/tex.png

model Box
  path models/box.gltf
  geometry static
  material none

model Hero
  path models/hero.glb # trailing comment
  geometry skinned
  material texture Tex
  preserve-aspect true
  rotate-half-turn true
  role player

model Belt
  path models/belt.gltf
  material primitive-texture-index 0
  primitive-textures true
  belt-scroll true

animation Idle
  path anims/idle.glb
  clip 8
  role player-idle

animation Move
  path anims/move.glb
  clip 7
  role player-move

animation Push
  path anims/push.glb
  clip 1
  role player-push

tile Wall
  model Box
  scale 1.25

tile Player
  model Hero
  scale 1.1

tile Ground
  scale 0.9

sound footsteps
  volume 0.3
  file audio/step with spaces 1.ogg
  file audio/step2.ogg

music 0
  file audio/track zero.ogg

music 2
  file audio/track two.ogg
  volume 0.8
)";

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
    check(manifest.model(belt).materialMode == sokoban::ModelMaterialMode::PrimitiveTextureIndex,
        "belt primitive material");

    check(manifest.playerIdleAnimation() == manifest.animationIdByName("Idle"), "idle role");
    check(manifest.playerMoveAnimation() == manifest.animationIdByName("Move"), "move role");
    check(manifest.playerPushAnimation() == manifest.animationIdByName("Push"), "push role");
    check(manifest.animation(manifest.playerIdleAnimation()).clip == 8, "idle clip");
    check(manifest.animation(manifest.playerIdleAnimation()).path == "anims/idle.glb",
        "trailing comment stripped from path");

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

void testValidationFailures()
{
    using sokoban::AssetManifest;
    const std::string base = validManifest;

    checkThrows([&] { (void)AssetManifest::parse("bogus Section\n"); }, "unknown section");
    checkThrows([&] { (void)AssetManifest::parse("path lost/property.png\n"); },
        "property outside section");
    checkThrows([&] { (void)AssetManifest::parse(base + "\ntile Wall\n  model Missing\n"); },
        "unknown tile model");
    checkThrows([&] { (void)AssetManifest::parse(base + "\ntile Bogus\n  scale 2\n"); },
        "unknown tile name");
    checkThrows([&] { (void)AssetManifest::parse(base + "\nmodel Hero2\n  path p.glb\n  role player\n  geometry skinned\n"); },
        "duplicate player role");
    checkThrows([&] { (void)AssetManifest::parse(base + "\nmodel Box\n  path q.gltf\n"); },
        "duplicate model name");
    checkThrows([&] { (void)AssetManifest::parse(base + "\nmusic 0\n  file again.ogg\n"); },
        "duplicate music level");
    checkThrows([&] { (void)AssetManifest::parse(base + "\nsound empty-set\n"); },
        "sound set without files");
    checkThrows([&] { (void)AssetManifest::parse(base + "\nsound bad-volume\n  volume -1\n  file f.ogg\n"); },
        "negative sound volume");
    checkThrows([&] { (void)AssetManifest::parse(base + "\nmodel NoTex\n  path p.gltf\n  material texture Ghost\n"); },
        "unknown material texture");
    checkThrows([&] { (void)AssetManifest::parse(base + "\nmodel BadIdx\n  path p.gltf\n  material primitive-texture-index 9\n"); },
        "primitive texture index out of range");

    // A manifest missing any player role must be rejected.
    checkThrows([&] {
        (void)AssetManifest::parse(
            "model Hero\n  path p.glb\n  geometry skinned\n  role player\n"
            "animation Idle\n  path i.glb\n  role player-idle\n"
            "animation Move\n  path m.glb\n  role player-move\n");
    }, "missing player-push role");
    checkThrows([&] { (void)AssetManifest::parse(""); }, "empty manifest lacks player");
}

void testRealManifestFile()
{
    const char* root = std::getenv("SOKOBAN_ASSETS");
    if (root == nullptr) {
        return; // Optional: only checked when the harness provides the path.
    }
    using sokoban::AssetManifest;
    const AssetManifest manifest =
        AssetManifest::loadFromFile(std::filesystem::path(root) / "manifest.txt");
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
    testValidationFailures();
    testRealManifestFile();

    if (failures != 0) {
        std::cerr << failures << " asset manifest checks failed\n";
        return 1;
    }
    std::cout << "All asset manifest checks passed\n";
    return 0;
}

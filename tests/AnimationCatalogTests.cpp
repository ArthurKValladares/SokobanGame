#include "engine/AnimationCatalog.hpp"
#include "engine/AssetManifest.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
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

std::filesystem::path assetRoot()
{
#ifdef _WIN32
    char* root = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&root, &length, "SOKOBAN_ASSETS") == 0 && root != nullptr) {
        const std::filesystem::path result = root;
        std::free(root);
        return result;
    }
#else
    if (const char* root = std::getenv("SOKOBAN_ASSETS")) {
        return root;
    }
#endif
    return "assets";
}

void testProductionCatalogIsCompleteAndRoundTrips()
{
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(assetRoot() / "manifest.json");
    sokoban::AnimationCatalog catalog =
        sokoban::AnimationCatalog::loadFromFile(
            assetRoot() / "animation_catalog.json", manifest);

    check(
        sokoban::animationUseDefinitions().size() ==
            static_cast<std::size_t>(sokoban::AnimationUse::Count),
        "every enum value has a definition");
    check(
        catalog.animation(sokoban::AnimationUse::EnemyIdle) ==
            manifest.animationIdByName("RogueIdle"),
        "enemy idle can share the player idle clip");

    const auto idle = manifest.animationIdByName("RogueIdle");
    catalog.setGlobalSpeed(idle, 1.5f);
    catalog.setUseSpeed(sokoban::AnimationUse::EnemyIdle, 0.5f);
    check(
        std::abs(catalog.effectiveSpeed(sokoban::AnimationUse::EnemyIdle) -
                 0.75f) < 0.0001f,
        "effective speed multiplies global and per-use controls");

    const std::string serialized = catalog.serialize(manifest);
    const sokoban::AnimationCatalog reparsed =
        sokoban::AnimationCatalog::parse(serialized, manifest);
    check(
        std::abs(reparsed.globalSpeed(idle) - 1.5f) < 0.0001f,
        "global speed round trips");
    check(
        std::abs(reparsed.useSpeed(sokoban::AnimationUse::EnemyIdle) - 0.5f) <
            0.0001f,
        "per-use speed round trips");
}

void testCatalogRejectsCodeAndManifestDrift()
{
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(assetRoot() / "manifest.json");
    const sokoban::AnimationCatalog valid =
        sokoban::AnimationCatalog::loadFromFile(
            assetRoot() / "animation_catalog.json", manifest);
    const std::string text = valid.serialize(manifest);

    std::string unknownUse = text;
    const std::string known = "player.idle";
    const std::size_t knownAt = unknownUse.find(known);
    unknownUse.replace(knownAt, known.size(), "player.typo");
    checkThrows(
        [&] { (void)sokoban::AnimationCatalog::parse(unknownUse, manifest); },
        "unknown code use rejected");

    std::string duplicateUse = text;
    const std::string move = "player.move";
    const std::size_t moveAt = duplicateUse.find(move);
    duplicateUse.replace(moveAt, move.size(), known);
    checkThrows(
        [&] { (void)sokoban::AnimationCatalog::parse(duplicateUse, manifest); },
        "duplicate and missing code use rejected");

    std::string unknownAnimation = text;
    const std::string idle = "RogueIdle";
    const std::size_t idleAt = unknownAnimation.find(idle);
    unknownAnimation.replace(idleAt, idle.size(), "MissingClip");
    checkThrows(
        [&] {
            (void)sokoban::AnimationCatalog::parse(unknownAnimation, manifest);
        },
        "unknown manifest animation rejected");
}

} // namespace

int main()
{
    testProductionCatalogIsCompleteAndRoundTrips();
    testCatalogRejectsCodeAndManifestDrift();

    if (failures != 0) {
        std::cerr << failures << " animation catalog checks failed\n";
        return 1;
    }
    std::cout << "Animation catalog checks passed\n";
    return 0;
}

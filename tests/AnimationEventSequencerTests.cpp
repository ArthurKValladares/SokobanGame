#include "engine/AnimationCatalog.hpp"
#include "engine/AnimationEventSequencer.hpp"
#include "engine/AssetManifest.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
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

} // namespace

int main()
{
    using namespace sokoban;
    const AssetManifest manifest =
        AssetManifest::loadFromFile(assetRoot() / "manifest.json");
    AnimationCatalog catalog = AnimationCatalog::loadFromFile(
        assetRoot() / "animation_catalog.json", manifest);
    catalog.setGlobalSpeed(
        catalog.animation(AnimationUse::EnemyAttack), 2.0f);
    catalog.setUseSpeed(AnimationUse::EnemyAttack, 1.5f);

    AnimationEventSequencer sequencer;
    constexpr uint64_t instance = 42;
    sequencer.begin(instance, AnimationUse::EnemyAttack);

    const float eventLogicalTime =
        catalog.eventSourceTime(
            AnimationUse::EnemyAttack, "attack-connected") /
        catalog.effectiveSpeed(AnimationUse::EnemyAttack);
    check(
        sequencer.advance(instance, eventLogicalTime - 0.01f, catalog).empty(),
        "event does not fire before marker");
    const auto fired =
        sequencer.advance(instance, eventLogicalTime + 0.02f, catalog);
    check(fired.size() == 1, "event fires when marker is crossed");
    if (!fired.empty()) {
        check(fired[0].instanceId == instance, "event retains instance id");
        check(fired[0].eventId == "attack-connected", "event retains id");
        check(
            std::abs(fired[0].overshootSeconds - 0.02f) < 0.0001f,
            "event reports frame overshoot");
    }
    check(
        sequencer.advance(instance, eventLogicalTime + 1.0f, catalog).empty(),
        "event fires only once per begin");

    sequencer.begin(instance, AnimationUse::EnemyAttack);
    check(
        sequencer.advance(instance, eventLogicalTime, catalog).size() == 1,
        "begin rearms instance events");
    sequencer.clear();
    check(
        sequencer.advance(instance, 10.0f, catalog).empty(),
        "clear removes active instances");

    if (failures != 0) {
        std::cerr << failures << " animation event sequencer checks failed\n";
        return 1;
    }
    std::cout << "Animation event sequencer checks passed\n";
    return 0;
}

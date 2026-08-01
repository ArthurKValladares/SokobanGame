#include "engine/AnimationCatalog.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/AtomicFile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace sokoban {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::array<AnimationUseDefinition, 13> useDefinitions {{
    { AnimationUse::PlayerIdle, "player.idle", "Player / Idle" },
    { AnimationUse::PlayerMove, "player.move", "Player / Move" },
    { AnimationUse::PlayerPush, "player.push", "Player / Push" },
    { AnimationUse::PlayerDeath, "player.death", "Player / Death" },
    { AnimationUse::PlayerDeadIdle, "player.dead-idle", "Player / Dead Idle" },
    { AnimationUse::EnemyIdle, "enemy.idle", "Enemy / Idle" },
    { AnimationUse::EnemyAttack, "enemy.attack", "Enemy / Attack" },
    { AnimationUse::MirrorPreviewPlayerIdle, "mirror-preview.player-idle", "Mirror Preview / Player Idle" },
    { AnimationUse::MirrorPreviewPlayerDeadIdle, "mirror-preview.player-dead-idle", "Mirror Preview / Player Dead Idle" },
    { AnimationUse::EditorPlayerIdle, "editor.player-idle", "Editor / Player Idle" },
    { AnimationUse::EditorEnemyIdle, "editor.enemy-idle", "Editor / Enemy Idle" },
    { AnimationUse::ThumbnailPlayerIdle, "thumbnail.player-idle", "Thumbnail / Player Idle" },
    { AnimationUse::ThumbnailEnemyIdle, "thumbnail.enemy-idle", "Thumbnail / Enemy Idle" },
}};
static_assert(
    useDefinitions.size() == static_cast<std::size_t>(AnimationUse::Count),
    "Every AnimationUse must have catalog metadata");

[[nodiscard]] std::size_t indexOf(AnimationUse use)
{
    const std::size_t index = static_cast<std::size_t>(use);
    if (index >= useDefinitions.size()) {
        throw std::out_of_range("invalid animation use");
    }
    return index;
}

[[nodiscard]] float validatedSpeed(float speed, std::string_view context)
{
    if (!std::isfinite(speed) || speed <= 0.0f || speed > 100.0f) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) +
            " speed must be finite and in (0, 100]");
    }
    return speed;
}

void rejectUnknown(
    const Json& value,
    std::initializer_list<std::string_view> allowed,
    std::string_view context)
{
    if (!value.is_object()) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) + " must be an object");
    }
    for (const auto& [key, ignored] : value.items()) {
        (void)ignored;
        if (std::ranges::none_of(allowed, [&](std::string_view candidate) {
                return candidate == key;
            })) {
            throw std::runtime_error(
                "animation catalog: " + std::string(context) +
                " has unknown property '" + key + "'");
        }
    }
}

[[nodiscard]] const Json& required(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) +
            " is missing '" + std::string(key) + "'");
    }
    return *it;
}

[[nodiscard]] std::string requiredString(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = required(object, key, context);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) + " '" +
            std::string(key) + "' must be a non-empty string");
    }
    return value.get<std::string>();
}

[[nodiscard]] float optionalSpeed(
    const Json& object,
    std::string_view context)
{
    const auto it = object.find("speed");
    if (it == object.end()) {
        return 1.0f;
    }
    if (!it->is_number()) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) +
            " 'speed' must be a number");
    }
    return validatedSpeed(it->get<float>(), context);
}

[[nodiscard]] AnimationUse useById(std::string_view id)
{
    const auto it = std::ranges::find(useDefinitions, id, &AnimationUseDefinition::id);
    if (it == useDefinitions.end()) {
        throw std::runtime_error(
            "animation catalog: unknown animation use '" + std::string(id) + "'");
    }
    return it->use;
}

} // namespace

std::span<const AnimationUseDefinition> animationUseDefinitions()
{
    return useDefinitions;
}

std::string_view animationUseId(AnimationUse use)
{
    return useDefinitions[indexOf(use)].id;
}

AnimationCatalog AnimationCatalog::parse(
    std::string_view text,
    const AssetManifest& manifest)
{
    try {
        const Json root = Json::parse(text);
        rejectUnknown(root, { "format", "clips", "uses" }, "root");
        const Json& format = required(root, "format", "root");
        if (!format.is_number_integer() || format.get<int>() != 1) {
            throw std::runtime_error("animation catalog: format must be 1");
        }
        const Json& clips = required(root, "clips", "root");
        const Json& uses = required(root, "uses", "root");
        if (!clips.is_array() || !uses.is_array()) {
            throw std::runtime_error(
                "animation catalog: 'clips' and 'uses' must be arrays");
        }

        AnimationCatalog catalog;
        catalog.globalSpeeds_.assign(manifest.animations().size(), 0.0f);
        for (std::size_t i = 0; i < clips.size(); ++i) {
            const std::string context = "clips[" + std::to_string(i) + "]";
            rejectUnknown(clips[i], { "animation", "speed" }, context);
            const std::string name = requiredString(clips[i], "animation", context);
            const RenderAnimation animation = manifest.animationIdByName(name);
            if (catalog.globalSpeeds_[animation.index()] != 0.0f) {
                throw std::runtime_error(
                    "animation catalog: duplicate clip '" + name + "'");
            }
            catalog.globalSpeeds_[animation.index()] = optionalSpeed(clips[i], context);
        }
        for (std::size_t i = 0; i < catalog.globalSpeeds_.size(); ++i) {
            if (catalog.globalSpeeds_[i] == 0.0f) {
                throw std::runtime_error(
                    "animation catalog: missing clip '" +
                    manifest.animations()[i].name + "'");
            }
        }

        std::array<bool, useCount> configured {};
        for (std::size_t i = 0; i < uses.size(); ++i) {
            const std::string context = "uses[" + std::to_string(i) + "]";
            rejectUnknown(uses[i], { "id", "animation", "speed" }, context);
            const AnimationUse use = useById(requiredString(uses[i], "id", context));
            const std::size_t useIndex = indexOf(use);
            if (configured[useIndex]) {
                throw std::runtime_error(
                    "animation catalog: duplicate use '" +
                    std::string(animationUseId(use)) + "'");
            }
            catalog.uses_[useIndex] = {
                .animation = manifest.animationIdByName(
                    requiredString(uses[i], "animation", context)),
                .speed = optionalSpeed(uses[i], context),
            };
            configured[useIndex] = true;
        }
        for (const AnimationUseDefinition& definition : useDefinitions) {
            if (!configured[indexOf(definition.use)]) {
                throw std::runtime_error(
                    "animation catalog: missing use '" +
                    std::string(definition.id) + "'");
            }
        }
        return catalog;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "animation catalog JSON error: " + std::string(error.what()));
    }
}

AnimationCatalog AnimationCatalog::loadFromFile(
    const std::filesystem::path& file,
    const AssetManifest& manifest)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open animation catalog " + file.string());
    }
    return parse(
        std::string(std::istreambuf_iterator<char>(stream), {}),
        manifest);
}

RenderAnimation AnimationCatalog::animation(AnimationUse use) const
{
    return uses_[indexOf(use)].animation;
}

float AnimationCatalog::globalSpeed(RenderAnimation animation) const
{
    if (animation.isNone() || animation.index() >= globalSpeeds_.size()) {
        throw std::out_of_range("invalid animation id for animation catalog");
    }
    return globalSpeeds_[animation.index()];
}

float AnimationCatalog::useSpeed(AnimationUse use) const
{
    return uses_[indexOf(use)].speed;
}

float AnimationCatalog::effectiveSpeed(AnimationUse use) const
{
    const UseBinding& binding = uses_[indexOf(use)];
    return globalSpeed(binding.animation) * binding.speed;
}

void AnimationCatalog::setGlobalSpeed(RenderAnimation animation, float speed)
{
    if (animation.isNone() || animation.index() >= globalSpeeds_.size()) {
        throw std::out_of_range("invalid animation id for animation catalog");
    }
    globalSpeeds_[animation.index()] = validatedSpeed(speed, "clip");
}

void AnimationCatalog::setUseAnimation(AnimationUse use, RenderAnimation animation)
{
    (void)globalSpeed(animation);
    uses_[indexOf(use)].animation = animation;
}

void AnimationCatalog::setUseSpeed(AnimationUse use, float speed)
{
    uses_[indexOf(use)].speed = validatedSpeed(speed, animationUseId(use));
}

std::string AnimationCatalog::serialize(const AssetManifest& manifest) const
{
    Json root = Json::object();
    root["format"] = 1;
    root["clips"] = Json::array();
    for (std::size_t i = 0; i < manifest.animations().size(); ++i) {
        root["clips"].push_back({
            { "animation", manifest.animations()[i].name },
            { "speed", globalSpeeds_.at(i) },
        });
    }
    root["uses"] = Json::array();
    for (const AnimationUseDefinition& definition : useDefinitions) {
        const UseBinding& binding = uses_[indexOf(definition.use)];
        root["uses"].push_back({
            { "id", definition.id },
            { "animation", manifest.animation(binding.animation).name },
            { "speed", binding.speed },
        });
    }
    return root.dump(2) + "\n";
}

void AnimationCatalog::save(
    const std::filesystem::path& file,
    const AssetManifest& manifest) const
{
    const std::string contents = serialize(manifest);
    (void)parse(contents, manifest);
    atomicFile::write(file, contents);
}

} // namespace sokoban

#include "engine/AnimationCatalog.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/AtomicFile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
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

[[nodiscard]] float validatedDuration(float duration, std::string_view context)
{
    if (!std::isfinite(duration) || duration < 0.0f || duration > 3600.0f) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) +
            " duration must be finite and in [0, 3600]");
    }
    return duration;
}

[[nodiscard]] float validatedNormalizedTime(
    float time,
    std::string_view context)
{
    if (!std::isfinite(time) || time < 0.0f || time > 1.0f) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) +
            " event time must be finite and in [0, 1]");
    }
    return time;
}

void validateEventId(std::string_view id, std::string_view context)
{
    if (id.empty() || std::ranges::any_of(id, [](char character) {
            return !(std::isalnum(static_cast<unsigned char>(character)) ||
                character == '-' || character == '_' || character == '.');
        })) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) +
            " event id must contain only letters, numbers, '.', '-', or '_'");
    }
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

[[nodiscard]] float requiredNumber(
    const Json& object,
    std::string_view key,
    std::string_view context)
{
    const Json& value = required(object, key, context);
    if (!value.is_number()) {
        throw std::runtime_error(
            "animation catalog: " + std::string(context) + " '" +
            std::string(key) + "' must be a number");
    }
    return value.get<float>();
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
        if (!format.is_number_integer() || format.get<int>() != 2) {
            throw std::runtime_error("animation catalog: format must be 2");
        }
        const Json& clips = required(root, "clips", "root");
        const Json& uses = required(root, "uses", "root");
        if (!clips.is_array() || !uses.is_array()) {
            throw std::runtime_error(
                "animation catalog: 'clips' and 'uses' must be arrays");
        }

        AnimationCatalog catalog;
        catalog.clips_.assign(manifest.animations().size(), {});
        std::vector<bool> configuredClips(manifest.animations().size(), false);
        for (std::size_t i = 0; i < clips.size(); ++i) {
            const std::string context = "clips[" + std::to_string(i) + "]";
            rejectUnknown(
                clips[i], { "animation", "speed", "duration" }, context);
            const std::string name = requiredString(clips[i], "animation", context);
            const RenderAnimation animation = manifest.animationIdByName(name);
            if (configuredClips[animation.index()]) {
                throw std::runtime_error(
                    "animation catalog: duplicate clip '" + name + "'");
            }
            catalog.clips_[animation.index()] = {
                .speed = optionalSpeed(clips[i], context),
                .durationSeconds = validatedDuration(
                    requiredNumber(clips[i], "duration", context), context),
            };
            configuredClips[animation.index()] = true;
        }
        for (std::size_t i = 0; i < catalog.clips_.size(); ++i) {
            if (!configuredClips[i]) {
                throw std::runtime_error(
                    "animation catalog: missing clip '" +
                    manifest.animations()[i].name + "'");
            }
        }

        std::array<bool, useCount> configured {};
        for (std::size_t i = 0; i < uses.size(); ++i) {
            const std::string context = "uses[" + std::to_string(i) + "]";
            rejectUnknown(
                uses[i],
                { "id", "animation", "speed", "events", "startAfter" },
                context);
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
                .events = {},
                .startAfter = std::nullopt
            };
            if (const auto events = uses[i].find("events");
                events != uses[i].end()) {
                if (!events->is_array()) {
                    throw std::runtime_error(
                        "animation catalog: " + context +
                        " 'events' must be an array");
                }
                for (std::size_t eventIndex = 0;
                     eventIndex < events->size();
                     ++eventIndex) {
                    const std::string eventContext = context + ".events[" +
                        std::to_string(eventIndex) + "]";
                    const Json& event = (*events)[eventIndex];
                    rejectUnknown(event, { "id", "at" }, eventContext);
                    std::string eventId =
                        requiredString(event, "id", eventContext);
                    validateEventId(eventId, eventContext);
                    catalog.uses_[useIndex].events.push_back({
                        .id = std::move(eventId),
                        .normalizedTime = validatedNormalizedTime(
                            requiredNumber(event, "at", eventContext),
                            eventContext),
                    });
                }
            }
            if (const auto startAfter = uses[i].find("startAfter");
                startAfter != uses[i].end()) {
                rejectUnknown(*startAfter, { "use", "event" },
                    context + ".startAfter");
                catalog.uses_[useIndex].startAfter = EventGate {
                    .sourceUse = useById(requiredString(
                        *startAfter, "use", context + ".startAfter")),
                    .eventId = requiredString(
                        *startAfter, "event", context + ".startAfter"),
                };
            }
            configured[useIndex] = true;
        }
        for (const AnimationUseDefinition& definition : useDefinitions) {
            if (!configured[indexOf(definition.use)]) {
                throw std::runtime_error(
                    "animation catalog: missing use '" +
                    std::string(definition.id) + "'");
            }
        }
        catalog.validateRelations();
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
    if (animation.isNone() || animation.index() >= clips_.size()) {
        throw std::out_of_range("invalid animation id for animation catalog");
    }
    return clips_[animation.index()].speed;
}

float AnimationCatalog::clipDuration(RenderAnimation animation) const
{
    if (animation.isNone() || animation.index() >= clips_.size()) {
        throw std::out_of_range("invalid animation id for animation catalog");
    }
    return clips_[animation.index()].durationSeconds;
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

std::span<const AnimationCatalog::TimelineEvent> AnimationCatalog::events(
    AnimationUse use) const
{
    return uses_[indexOf(use)].events;
}

const std::optional<AnimationCatalog::EventGate>& AnimationCatalog::startGate(
    AnimationUse use) const
{
    return uses_[indexOf(use)].startAfter;
}

float AnimationCatalog::eventSourceTime(
    AnimationUse use,
    std::string_view eventId) const
{
    const UseBinding& binding = uses_[indexOf(use)];
    const auto event = std::ranges::find(
        binding.events, eventId, &TimelineEvent::id);
    if (event == binding.events.end()) {
        throw std::out_of_range(
            "unknown animation event '" + std::string(eventId) + "' on use '" +
            std::string(animationUseId(use)) + "'");
    }
    return clipDuration(binding.animation) * event->normalizedTime;
}

void AnimationCatalog::setGlobalSpeed(RenderAnimation animation, float speed)
{
    if (animation.isNone() || animation.index() >= clips_.size()) {
        throw std::out_of_range("invalid animation id for animation catalog");
    }
    clips_[animation.index()].speed = validatedSpeed(speed, "clip");
}

void AnimationCatalog::setClipDuration(
    RenderAnimation animation,
    float durationSeconds)
{
    (void)globalSpeed(animation);
    const float previous = clips_[animation.index()].durationSeconds;
    clips_[animation.index()].durationSeconds =
        validatedDuration(durationSeconds, "clip");
    try {
        validateRelations();
    } catch (...) {
        clips_[animation.index()].durationSeconds = previous;
        throw;
    }
}

void AnimationCatalog::setUseAnimation(AnimationUse use, RenderAnimation animation)
{
    (void)globalSpeed(animation);
    UseBinding& binding = uses_[indexOf(use)];
    const RenderAnimation previous = binding.animation;
    binding.animation = animation;
    try {
        validateRelations();
    } catch (...) {
        binding.animation = previous;
        throw;
    }
}

void AnimationCatalog::setUseSpeed(AnimationUse use, float speed)
{
    uses_[indexOf(use)].speed = validatedSpeed(speed, animationUseId(use));
}

void AnimationCatalog::setTimelineEvent(
    AnimationUse use,
    std::string eventId,
    float normalizedTime)
{
    validateEventId(eventId, animationUseId(use));
    normalizedTime = validatedNormalizedTime(
        normalizedTime, animationUseId(use));
    UseBinding& binding = uses_[indexOf(use)];
    if (clipDuration(binding.animation) <= 0.0f) {
        throw std::runtime_error(
            "animation catalog: events require a non-zero clip duration");
    }
    const auto existing = std::ranges::find(
        binding.events, eventId, &TimelineEvent::id);
    if (existing == binding.events.end()) {
        binding.events.push_back({
            .id = std::move(eventId),
            .normalizedTime = normalizedTime,
        });
    } else {
        existing->normalizedTime = normalizedTime;
    }
    std::ranges::sort(binding.events, [](const TimelineEvent& left,
                                         const TimelineEvent& right) {
        if (left.normalizedTime != right.normalizedTime) {
            return left.normalizedTime < right.normalizedTime;
        }
        return left.id < right.id;
    });
}

void AnimationCatalog::updateTimelineEvent(
    AnimationUse use,
    std::string_view originalEventId,
    std::string eventId,
    float normalizedTime)
{
    validateEventId(originalEventId, animationUseId(use));
    validateEventId(eventId, animationUseId(use));
    normalizedTime = validatedNormalizedTime(
        normalizedTime, animationUseId(use));

    const auto previousUses = uses_;
    try {
        UseBinding& binding = uses_[indexOf(use)];
        const auto existing = std::ranges::find(
            binding.events, originalEventId, &TimelineEvent::id);
        if (existing == binding.events.end()) {
            throw std::runtime_error(
                "animation catalog: event '" +
                std::string(originalEventId) + "' does not exist on use '" +
                std::string(animationUseId(use)) + "'");
        }
        const auto duplicate = std::ranges::find(
            binding.events, eventId, &TimelineEvent::id);
        if (duplicate != binding.events.end() && duplicate != existing) {
            throw std::runtime_error(
                "animation catalog: duplicate event '" + eventId +
                "' on use '" + std::string(animationUseId(use)) + "'");
        }

        const std::string oldId = existing->id;
        existing->id = std::move(eventId);
        existing->normalizedTime = normalizedTime;
        for (UseBinding& candidate : uses_) {
            if (candidate.startAfter &&
                candidate.startAfter->sourceUse == use &&
                candidate.startAfter->eventId == oldId) {
                candidate.startAfter->eventId = existing->id;
            }
        }
        std::ranges::sort(binding.events, [](const TimelineEvent& left,
                                             const TimelineEvent& right) {
            if (left.normalizedTime != right.normalizedTime) {
                return left.normalizedTime < right.normalizedTime;
            }
            return left.id < right.id;
        });
        validateRelations();
    } catch (...) {
        uses_ = previousUses;
        throw;
    }
}

void AnimationCatalog::removeTimelineEvent(
    AnimationUse use,
    std::string_view eventId)
{
    UseBinding& binding = uses_[indexOf(use)];
    std::erase_if(binding.events, [&](const TimelineEvent& event) {
        return event.id == eventId;
    });
    for (UseBinding& candidate : uses_) {
        if (candidate.startAfter &&
            candidate.startAfter->sourceUse == use &&
            candidate.startAfter->eventId == eventId) {
            candidate.startAfter.reset();
        }
    }
}

void AnimationCatalog::setStartGate(
    AnimationUse use,
    std::optional<EventGate> gate)
{
    UseBinding& binding = uses_[indexOf(use)];
    const std::optional<EventGate> previous = binding.startAfter;
    binding.startAfter = std::move(gate);
    try {
        validateRelations();
    } catch (...) {
        binding.startAfter = previous;
        throw;
    }
}

std::string AnimationCatalog::serialize(const AssetManifest& manifest) const
{
    validateRelations();
    Json root = Json::object();
    root["format"] = 2;
    root["clips"] = Json::array();
    for (std::size_t i = 0; i < manifest.animations().size(); ++i) {
        root["clips"].push_back({
            { "animation", manifest.animations()[i].name },
            { "speed", clips_.at(i).speed },
            { "duration", clips_.at(i).durationSeconds },
        });
    }
    root["uses"] = Json::array();
    for (const AnimationUseDefinition& definition : useDefinitions) {
        const UseBinding& binding = uses_[indexOf(definition.use)];
        Json use = {
            { "id", definition.id },
            { "animation", manifest.animation(binding.animation).name },
            { "speed", binding.speed },
        };
        if (!binding.events.empty()) {
            use["events"] = Json::array();
            for (const TimelineEvent& event : binding.events) {
                use["events"].push_back({
                    { "id", event.id },
                    { "at", event.normalizedTime },
                });
            }
        }
        if (binding.startAfter) {
            use["startAfter"] = {
                { "use", animationUseId(binding.startAfter->sourceUse) },
                { "event", binding.startAfter->eventId },
            };
        }
        root["uses"].push_back(std::move(use));
    }
    return root.dump(2) + "\n";
}

void AnimationCatalog::validateRelations() const
{
    for (const AnimationUseDefinition& definition : useDefinitions) {
        const UseBinding& binding = uses_[indexOf(definition.use)];
        (void)globalSpeed(binding.animation);
        if (!binding.events.empty() && clipDuration(binding.animation) <= 0.0f) {
            throw std::runtime_error(
                "animation catalog: use '" + std::string(definition.id) +
                "' has events on a zero-duration clip");
        }
        std::vector<std::string_view> eventIds;
        eventIds.reserve(binding.events.size());
        for (const TimelineEvent& event : binding.events) {
            validateEventId(event.id, definition.id);
            (void)validatedNormalizedTime(event.normalizedTime, definition.id);
            if (std::ranges::find(eventIds, event.id) != eventIds.end()) {
                throw std::runtime_error(
                    "animation catalog: duplicate event '" + event.id +
                    "' on use '" + std::string(definition.id) + "'");
            }
            eventIds.push_back(event.id);
        }
        if (binding.startAfter) {
            validateEventId(binding.startAfter->eventId, definition.id);
            const UseBinding& source =
                uses_[indexOf(binding.startAfter->sourceUse)];
            if (std::ranges::find(
                    source.events,
                    binding.startAfter->eventId,
                    &TimelineEvent::id) == source.events.end()) {
                throw std::runtime_error(
                    "animation catalog: use '" + std::string(definition.id) +
                    "' waits for missing event '" +
                    binding.startAfter->eventId + "' on use '" +
                    std::string(animationUseId(
                        binding.startAfter->sourceUse)) + "'");
            }
        }
    }

    enum class Visit : uint8_t { Unvisited, Visiting, Complete };
    std::array<Visit, useCount> visits {};
    std::function<void(AnimationUse)> visit = [&](AnimationUse use) {
        const std::size_t useIndex = indexOf(use);
        if (visits[useIndex] == Visit::Complete) {
            return;
        }
        if (visits[useIndex] == Visit::Visiting) {
            throw std::runtime_error(
                "animation catalog: startAfter dependencies contain a cycle");
        }
        visits[useIndex] = Visit::Visiting;
        if (uses_[useIndex].startAfter) {
            visit(uses_[useIndex].startAfter->sourceUse);
        }
        visits[useIndex] = Visit::Complete;
    };
    for (const AnimationUseDefinition& definition : useDefinitions) {
        visit(definition.use);
    }
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

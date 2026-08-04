#pragma once

#include "engine/SettingsTypes.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace sokoban {

// Upgrades a parsed profile document in place from `sourceFormat` to the
// current format. The caller validates the source format before calling.
void migratePlayerProfileToCurrent(
    nlohmann::json& root,
    int sourceFormat);

// Shared codec operations used by migrations that introduce or rewrite input
// bindings. Kept internal to the profile codec and migration implementation.
namespace playerProfileMigrationSupport {

[[noreturn]] void fail(
    std::string_view context,
    const std::string& message);
const nlohmann::json& requiredProperty(
    const nlohmann::json& object,
    std::string_view key,
    std::string_view context);
int nonNegativeIntegerProperty(
    const nlohmann::json& object,
    std::string_view key,
    std::string_view context);
std::string stringProperty(
    const nlohmann::json& object,
    std::string_view key,
    std::string_view context);
InputBindings inputBindingsFromJson(
    const nlohmann::json& value,
    std::string_view context,
    bool includeMenuConfirm = true);
nlohmann::ordered_json inputBindingsToJson(const InputBindings& bindings);

} // namespace playerProfileMigrationSupport
} // namespace sokoban

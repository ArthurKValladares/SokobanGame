#include "engine/DecorationMeshCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_map>

namespace sokoban {
namespace {

std::string lowercase(std::string value)
{
    std::ranges::transform(
        value,
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string pathKey(const std::filesystem::path& path)
{
    std::string key = path.lexically_normal().generic_string();
#ifdef _WIN32
    key = lowercase(std::move(key));
#endif
    return key;
}

bool supportedMesh(const std::filesystem::path& path)
{
    const std::string extension = lowercase(path.extension().string());
    return extension == ".gltf" || extension == ".glb";
}

} // namespace

bool DecorationMeshCatalog::refresh(
    const std::filesystem::path& sourceAssetRoot,
    const AssetManifest& manifest)
{
    entries_.clear();
    std::error_code error;
    if (!std::filesystem::is_directory(sourceAssetRoot, error)) {
        status_ = "Source asset directory is unavailable: " +
            sourceAssetRoot.string();
        return false;
    }

    std::unordered_map<std::string, std::string> registeredModels;
    registeredModels.reserve(manifest.models().size());
    for (const AssetManifest::Model& model : manifest.models()) {
        registeredModels.try_emplace(pathKey(model.path), model.name);
    }

    std::filesystem::recursive_directory_iterator iterator(
        sourceAssetRoot,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (error) {
            error.clear();
            iterator.increment(error);
            continue;
        }
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(error) && !error &&
            supportedMesh(entry.path())) {
            const std::filesystem::path relative =
                entry.path().lexically_relative(sourceAssetRoot);
            const auto registered = registeredModels.find(pathKey(relative));
            entries_.push_back({
                .relativePath = relative,
                .modelName = registered == registeredModels.end()
                    ? std::string {}
                    : registered->second,
            });
        }
        error.clear();
        iterator.increment(error);
    }

    std::ranges::sort(
        entries_,
        [](const Entry& left, const Entry& right) {
            if (left.registered() != right.registered()) {
                return left.registered() > right.registered();
            }
            return left.relativePath.generic_string() <
                right.relativePath.generic_string();
        });
    const std::size_t registeredCount = static_cast<std::size_t>(
        std::ranges::count_if(entries_, &Entry::registered));
    status_ = "Found " + std::to_string(entries_.size()) +
        " mesh files (" + std::to_string(registeredCount) +
        " registered in the manifest).";
    return true;
}

} // namespace sokoban

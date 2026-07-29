#include "engine/DecorationAssetRegistry.hpp"

#include "engine/AtomicFile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace sokoban {
namespace {

using Json = nlohmann::json;

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
    std::string result = path.lexically_normal().generic_string();
#ifdef _WIN32
    result = lowercase(std::move(result));
#endif
    return result;
}

bool supportedMesh(const std::filesystem::path& path)
{
    const std::string extension = lowercase(path.extension().string());
    return extension == ".gltf" || extension == ".glb";
}

bool escapesRoot(const std::filesystem::path& relative)
{
    if (relative.empty() || relative.is_absolute()) {
        return true;
    }
    const std::filesystem::path normalized = relative.lexically_normal();
    return normalized.empty() || *normalized.begin() == "..";
}

std::filesystem::path dependencyPath(
    const std::filesystem::path& relativeMesh,
    std::string_view uri)
{
    if (uri.empty() || uri.find('\\') != std::string_view::npos ||
        uri.find('%') != std::string_view::npos ||
        uri.find("://") != std::string_view::npos) {
        throw std::runtime_error(
            "unsupported external glTF URI '" + std::string(uri) + "'");
    }
    const std::filesystem::path dependency =
        (relativeMesh.parent_path() / uri).lexically_normal();
    if (escapesRoot(dependency)) {
        throw std::runtime_error(
            "glTF dependency escapes the asset root: " + std::string(uri));
    }
    return dependency;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open " + path.string());
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void collectUris(const Json& value, std::vector<std::string>& uris)
{
    if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            if (key == "uri" && child.is_string()) {
                uris.push_back(child.get<std::string>());
            } else {
                collectUris(child, uris);
            }
        }
    } else if (value.is_array()) {
        for (const Json& child : value) {
            collectUris(child, uris);
        }
    }
}

std::vector<std::filesystem::path> meshFiles(
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& relativeMesh)
{
    if (escapesRoot(relativeMesh) || !supportedMesh(relativeMesh)) {
        throw std::runtime_error(
            "decoration mesh must be an asset-relative .gltf or .glb path");
    }

    std::vector<std::filesystem::path> files { relativeMesh.lexically_normal() };
    if (lowercase(relativeMesh.extension().string()) != ".gltf") {
        return files;
    }

    const std::filesystem::path source = sourceRoot / relativeMesh;
    const Json root = Json::parse(readText(source));
    std::vector<std::string> uris;
    collectUris(root, uris);
    for (const std::string& uri : uris) {
        if (uri.starts_with("data:")) {
            continue;
        }
        files.push_back(dependencyPath(relativeMesh, uri));
    }
    std::ranges::sort(files);
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

void copyMeshFiles(
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& runtimeRoot,
    const std::vector<std::filesystem::path>& files)
{
    for (const std::filesystem::path& relative : files) {
        const std::filesystem::path source = sourceRoot / relative;
        std::error_code error;
        if (!std::filesystem::is_regular_file(source, error) || error) {
            throw std::runtime_error(
                "decoration asset is missing: " + source.string());
        }
        const std::filesystem::path destination = runtimeRoot / relative;
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing);
    }
}

const AssetManifest::Model* modelForPath(
    const std::vector<AssetManifest::Model>& models,
    const std::filesystem::path& relativePath)
{
    const std::string key = pathKey(relativePath);
    const auto found = std::ranges::find_if(
        models,
        [&](const AssetManifest::Model& model) {
            return pathKey(model.path) == key;
        });
    return found == models.end() ? nullptr : &*found;
}

std::optional<std::size_t> modelIndexForPath(
    const std::vector<AssetManifest::Model>& models,
    const std::filesystem::path& relativePath)
{
    const std::string key = pathKey(relativePath);
    for (std::size_t index = 0; index < models.size(); ++index) {
        if (pathKey(models[index].path) == key) {
            return index;
        }
    }
    return std::nullopt;
}

const AssetManifest::Texture* textureForPath(
    const std::vector<AssetManifest::Texture>& textures,
    const std::filesystem::path& relativePath)
{
    const std::string key = pathKey(relativePath);
    const auto found = std::ranges::find_if(
        textures,
        [&](const AssetManifest::Texture& texture) {
            return pathKey(texture.path) == key;
        });
    return found == textures.end() ? nullptr : &*found;
}

bool modelNameExists(
    const std::vector<AssetManifest::Model>& models,
    std::string_view name)
{
    return std::ranges::any_of(
        models,
        [&](const AssetManifest::Model& model) {
            return model.name == name;
        });
}

std::string generatedModelName(
    const std::filesystem::path& relativePath,
    const std::vector<AssetManifest::Model>& models)
{
    std::string stem = relativePath.stem().string();
    for (char& character : stem) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value)) {
            character = '_';
        }
    }
    if (stem.empty()) {
        stem = "Mesh";
    }
    const std::string base = "Decoration_" + stem;
    std::string candidate = base;
    for (std::size_t suffix = 2; modelNameExists(models, candidate); ++suffix) {
        candidate = base + "_" + std::to_string(suffix);
    }
    return candidate;
}

std::string generatedTextureName(
    const std::filesystem::path& relativePath,
    const std::vector<AssetManifest::Texture>& textures)
{
    std::string stem = relativePath.stem().string();
    for (char& character : stem) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value)) {
            character = '_';
        }
    }
    if (stem.empty()) {
        stem = "Texture";
    }
    const std::string base = "DecorationTexture_" + stem;
    auto available = [&](std::string_view candidate) {
        return std::ranges::none_of(
            textures,
            [&](const AssetManifest::Texture& texture) {
                return texture.name == candidate;
            });
    };
    std::string candidate = base;
    for (std::size_t suffix = 2; !available(candidate); ++suffix) {
        candidate = base + "_" + std::to_string(suffix);
    }
    return candidate;
}

std::optional<std::filesystem::path> singleBaseColorTexture(
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& relativeMesh)
{
    if (lowercase(relativeMesh.extension().string()) != ".gltf") {
        return std::nullopt;
    }

    const Json root = Json::parse(readText(sourceRoot / relativeMesh));
    if (!root.contains("materials") || !root["materials"].is_array() ||
        !root.contains("textures") || !root["textures"].is_array() ||
        !root.contains("images") || !root["images"].is_array()) {
        return std::nullopt;
    }

    std::vector<std::size_t> textureIndices;
    for (const Json& material : root["materials"]) {
        if (!material.is_object() ||
            !material.contains("pbrMetallicRoughness")) {
            continue;
        }
        const Json& pbr = material["pbrMetallicRoughness"];
        if (!pbr.is_object() || !pbr.contains("baseColorTexture")) {
            continue;
        }
        const Json& baseColor = pbr["baseColorTexture"];
        if (!baseColor.is_object() || !baseColor.contains("index") ||
            !baseColor["index"].is_number_unsigned()) {
            continue;
        }
        textureIndices.push_back(baseColor["index"].get<std::size_t>());
    }
    std::ranges::sort(textureIndices);
    textureIndices.erase(
        std::unique(textureIndices.begin(), textureIndices.end()),
        textureIndices.end());
    if (textureIndices.size() != 1 ||
        textureIndices.front() >= root["textures"].size()) {
        return std::nullopt;
    }

    const Json& texture = root["textures"][textureIndices.front()];
    if (!texture.is_object() || !texture.contains("source") ||
        !texture["source"].is_number_unsigned()) {
        return std::nullopt;
    }
    const std::size_t imageIndex = texture["source"].get<std::size_t>();
    if (imageIndex >= root["images"].size()) {
        return std::nullopt;
    }
    const Json& image = root["images"][imageIndex];
    if (!image.is_object() || !image.contains("uri") ||
        !image["uri"].is_string()) {
        return std::nullopt;
    }
    const std::string uri = image["uri"].get<std::string>();
    if (uri.starts_with("data:")) {
        return std::nullopt;
    }
    return dependencyPath(relativeMesh, uri);
}

} // namespace

DecorationAssetRegistry::Result DecorationAssetRegistry::registerMesh(
    const Request& request)
{
    try {
        const std::filesystem::path relative =
            request.relativeMeshPath.lexically_normal();
        const std::vector<std::filesystem::path> files = meshFiles(
            request.sourceAssetRoot, relative);
        copyMeshFiles(
            request.sourceAssetRoot, request.runtimeAssetRoot, files);

        bool editorChanged = false;
        bool textureAdded = false;
        std::optional<AssetManifest::Texture> materialTexture;
        if (const std::optional<std::filesystem::path> texturePath =
                singleBaseColorTexture(request.sourceAssetRoot, relative)) {
            if (const AssetManifest::Texture* existing = textureForPath(
                    request.manifestEditor.textures(), *texturePath)) {
                materialTexture = *existing;
            } else {
                materialTexture = AssetManifest::Texture {
                    .name = generatedTextureName(
                        *texturePath, request.manifestEditor.textures()),
                    .path = texturePath->generic_string(),
                };
                request.manifestEditor.addTexture();
                request.manifestEditor.updateTexture(
                    request.manifestEditor.textures().size() - 1,
                    *materialTexture);
                editorChanged = true;
                textureAdded = true;
            }
        }

        AssetManifest::Model model;
        bool added = false;
        const std::optional<std::size_t> sourceModelIndex =
            modelIndexForPath(request.manifestEditor.models(), relative);
        if (sourceModelIndex) {
            model = request.manifestEditor.models()[*sourceModelIndex];
            if (model.name.starts_with("Decoration_")) {
                const bool needsScaleUpgrade = !model.preserveSourceScale;
                const bool needsMaterialUpgrade =
                    materialTexture.has_value() &&
                    model.materialMode == ModelMaterialMode::Untextured;
                if (needsScaleUpgrade || needsMaterialUpgrade) {
                    model.preserveSourceScale = true;
                    if (needsMaterialUpgrade) {
                        model.materialMode = ModelMaterialMode::SingleTexture;
                        model.materialTextureName = materialTexture->name;
                    }
                    request.manifestEditor.updateModel(*sourceModelIndex, model);
                    editorChanged = true;
                }
            }
        } else {
            model = {
                .name = generatedModelName(
                    relative, request.manifestEditor.models()),
                .path = relative.generic_string(),
                .preserveSourceScale = true,
            };
            if (materialTexture) {
                model.materialMode = ModelMaterialMode::SingleTexture;
                model.materialTextureName = materialTexture->name;
            }
            request.manifestEditor.addModel();
            request.manifestEditor.updateModel(
                request.manifestEditor.models().size() - 1, model);
            editorChanged = true;
            added = true;
        }

        if (editorChanged) {
            if (!request.manifestEditor.save()) {
                const std::string failure = request.manifestEditor.status();
                (void)request.manifestEditor.reload();
                throw std::runtime_error(failure);
            }
        }

        if (materialTexture &&
            request.runtimeManifest.findTextureIdByName(
                materialTexture->name).isNone()) {
            if (request.runtimeManifest.addTexture(*materialTexture).isNone()) {
                throw std::runtime_error(
                    "could not append decoration texture to the live manifest");
            }
        }

        const AssetManifest::Model* runtimeModel =
            modelForPath(request.runtimeManifest.models(), relative);
        if (!runtimeModel) {
            const RenderModel addedModel =
                request.runtimeManifest.addModel(model);
            if (addedModel.isCube()) {
                throw std::runtime_error(
                    "could not append decoration model to the live manifest");
            }
            runtimeModel = &request.runtimeManifest.model(addedModel);
        }

        std::string status = added
            ? "Added " + model.name + " to the manifest."
            : "Loaded existing source-manifest model " + model.name + ".";
        if (textureAdded) {
            status += " Registered texture " + materialTexture->name + ".";
        }
        if (editorChanged && !added && runtimeModel->preserveSourceScale !=
                model.preserveSourceScale) {
            status += " Restart the editor to reload the upgraded model.";
        }
        try {
            atomicFile::write(
                request.runtimeAssetRoot / "manifest.json",
                request.manifestEditor.serialize());
        } catch (const std::exception& error) {
            status += " The staged manifest could not be updated: " +
                std::string(error.what());
        }

        return {
            .succeeded = true,
            .added = added,
            .modelName = runtimeModel->name,
            .status = std::move(status),
        };
    } catch (const std::exception& error) {
        return {
            .status = "Could not register decoration mesh: " +
                std::string(error.what()),
        };
    }
}

} // namespace sokoban

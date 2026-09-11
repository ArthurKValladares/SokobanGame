#include "engine/AssetManifest.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/Log.hpp"
#include "engine/SplatPainter.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/PngWriter.hpp"
#include "engine/render/TextureSourceLoader.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace sokoban;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
int runProbe(int argc, char** argv) {
    require(argc == 3, "usage: probe fixture-root repository-root");
    const std::filesystem::path fixtures = std::filesystem::absolute(argv[1]);
    const std::filesystem::path repository = argv[2];
    require(!std::filesystem::exists(fixtures), "fixture root must be new");
    const auto source = fixtures / "levels-source";
    const auto runtime = fixtures / "levels-runtime";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(runtime);
    LevelEditor editor;
    editor.initialize(source, runtime, 0, 0);
    editor.newDocument(3, 3);
    require(editor.saveDocument(source / "level0/screen0.scr").succeeded(), "save zero");
    editor.newDocument(3, 3);
    require(editor.saveDocument(source / "level0/screen1.scr").succeeded(), "save one");
    editor.setCell({1, 1, 1}, TileType::Wall);
    require(editor.openDocument(source / "level0/screen0.scr"), "switch to zero");
    require(editor.hasInProgressDraft(source / "level0/screen1.scr"), "draft cached");
    editor.addScreenAt(editor.collectLevelDirectories().front(), 0);
    require(std::filesystem::exists(source / "level0/screen2.scr"), "insert succeeded");
    std::cout << "after_insert_cached_draft_old_path=" << editor.hasInProgressDraft(source / "level0/screen1.scr") << '\n';
    std::cout << "after_insert_cached_draft_new_path=" << editor.hasInProgressDraft(source / "level0/screen2.scr") << '\n';
    const auto disk = Level::loadFromFile(source / "level0/screen1.scr");
    require(editor.openDocument(source / "level0/screen1.scr"), "open new occupant");
    std::cout << "wrong_occupant_disk_wall=" << (disk.authoredTileAt(1, 1, 1) == TileType::Wall) << '\n';
    std::cout << "wrong_occupant_restored_draft_wall=" << (editor.documentDefinition().layers[1][1][1] == tileTypeToChar(TileType::Wall)) << '\n';

    AssetManifest manifest = AssetManifest::loadFromFile(repository / "assets/manifest.json");
    AssetManifest::Texture texture{.name = "ReviewPaint", .path = "review-paint.png", .filter = TextureFilter::Linear, .colorSpace = TextureColorSpace::Linear};
    require(!manifest.addTexture(texture).isNone(), "register probe texture");
    const auto assetSource = fixtures / "assets-source";
    const auto assetRuntime = fixtures / "assets-runtime";
    std::filesystem::create_directories(assetSource);
    std::filesystem::create_directories(assetRuntime);
    std::filesystem::copy_file(repository / "assets/manifest.json", assetRuntime / "manifest.json");
    auto blank = SplatCanvas::createForBoard(1, 1);
    writeGrayscalePng(assetSource / texture.path, blank.width(), blank.height(), blank.weights());
    writeGrayscalePng(assetRuntime / texture.path, blank.width(), blank.height(), blank.weights());
    const auto identity = manifestTextureSourceIdentity(texture, texture.path);
    const auto oldArtifact = buildBc7Ktx2(blank.toImage(), identity.interpretation);
    writeBytes(assetRuntime / compressedTextureArtifactPath(identity), oldArtifact);
    std::ofstream(assetRuntime / "content.index", std::ios::binary) << "format 1\ngame-version review\n";
    require(refreshContentPackageIndex(assetRuntime), "index fixture");
    SplatPainter painter;
    require(painter.open({.documentPath = source / "level0/screen0.scr", .boardTilesWide = 1, .boardTilesHigh = 1, .sourceAssetRoot = assetSource, .runtimeAssetRoot = assetRuntime, .textureName = "ReviewPaint"}, manifest), "open painter");
    painter.brush() = {.radiusTiles = 2.0f, .hardness = 1.0f, .opacity = 1.0f, .color = SplatCanvas::BrushColor::White};
    require(painter.beginStroke({0.5f, 0.5f}), "paint white");
    painter.endStroke();
    require(painter.save(), "save paint");
    validateContentPackage(assetRuntime, "review");
    const auto raw = loadRgbaTextureSource(assetRuntime, identity.source);
    const auto prepared = loadPreparedTextureSource(assetRuntime, identity, true);
    std::cout << "saved_raw_first_pixel=" << std::to_integer<int>(raw.rgba.front()) << '\n';
    std::cout << "restart_uses_compressed=" << std::holds_alternative<CompressedTextureArtifact>(prepared) << '\n';
    std::cout << "restart_compressed_is_old_black=" << (std::get<CompressedTextureArtifact>(prepared).mips.front().bytes == parseBc7Ktx2(oldArtifact).mips.front().bytes) << '\n';
    std::cout << "refreshed_package_validation_passed=1\n";
    return 0;
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    int result = 0;
    try { result = runProbe(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << "probe failed: " << error.what() << '\n';
        result = 1;
    }
    log::shutdown();
    return result;
}

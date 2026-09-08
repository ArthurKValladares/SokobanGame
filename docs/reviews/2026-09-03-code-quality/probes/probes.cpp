#include "engine/SaveStore.hpp"
#include "engine/SaveSlotManager.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/ActionScheduler.hpp"
#include "engine/InputRouter.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/DecorationAssetRegistry.hpp"
#include "engine/AssetManifestEditor.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/ContentPipeline.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;
using namespace sokoban;
void write(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }
PlayerProfile progress(int level) {
    PlayerProfile p; p.unlockedLevel = level; p.setCurrentScreen(level, 0);
    p.recordReachedScreen(level, 0); p.normalize(); return p;
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    if (argc != 3) {
        std::cerr << "Usage: review_probes NEW_OUTPUT_DIRECTORY FIXTURE_DIRECTORY\n";
        return 2;
    }
    const fs::path root = fs::absolute(argv[1]);
    const fs::path fixtures = fs::absolute(argv[2]);
    if (fs::exists(root)) { std::cerr << "Refusing an existing probe directory\n"; return 2; }
    fs::create_directories(root);
    {
        const auto dir = root / "migration"; fs::create_directories(dir);
        SaveStore s(dir);
        const std::string legacy = R"({"format":1,"unlockedLevel":3,"currentLevel":2,"completedLevels":[0],"masterVolume":0.5,"musicVolume":0.25,"soundVolume":0.75})";
        std::cout << "legacy fixture decodes to level=" << decodePlayerProfile(legacy).profile.currentLevel << '\n';
        write(s.primaryPath(), legacy);
        atomicFile::failWriteAfterForTesting(0, std::errc::no_space_on_device);
        auto r = s.load();
        std::cout << "migration write failure: disposition=" << int(r.disposition)
                  << " level=" << r.profile.currentLevel << " message=" << r.message << '\n';
        for (const auto& f : fs::directory_iterator(dir)) std::cout << "  " << f.path().filename() << '\n';
    }
    {
        const auto dir = root / "recovery"; fs::create_directories(dir);
        SaveStore s(dir); write(s.backupPath(), progress(3).serialize());
        atomicFile::failWriteAfterForTesting(0, std::errc::no_space_on_device);
        auto r = s.load();
        std::cout << "backup promotion write failure: disposition=" << int(r.disposition)
                  << " level=" << r.profile.currentLevel << " message=" << r.message << '\n';
        for (const auto& f : fs::directory_iterator(dir)) std::cout << "  " << f.path().filename() << '\n';
    }
    {
        const auto dir = root / "deletion"; fs::create_directories(dir);
        SaveSlotManager m(dir, std::chrono::milliseconds(0));
        const auto p = m.loadActiveProfile();
        write(dir / "profile-slot2.json", progress(2).serialize());
        write(dir / "profile-slot2.json.tmp", progress(3).serialize());
        const auto deleted = m.deleteSlot(1);
        const auto next = m.switchTo(1, p);
        std::cout << "deleted slot: success=" << deleted.succeeded
                  << " restored level=" << (next ? next->currentLevel : -1)
                  << " empty=" << (next ? next->progressEmpty() : true) << '\n';
    }
    {
        const auto dir = root / "switch"; fs::create_directories(dir);
        SaveSlotManager m(dir, std::chrono::hours(1));
        auto p = m.loadActiveProfile();
        m.saveProgress(progress(1), true); m.flush();
        atomicFile::failWriteAfterForTesting(0, std::errc::no_space_on_device);
        m.saveProgress(progress(3), false);
        auto next = m.switchTo(1, progress(3));
        const auto saved = SaveStore(dir).inspect();
        std::cout << "switch after outgoing save failure: switched=" << next.has_value()
                  << " active=" << m.activeSlot()
                  << " disk level=" << (saved.profile ? saved.profile->currentLevel : -1)
                  << " status=" << m.progressStatus() << '\n';
    }
    {
        InputState input(false); InputRouter router;
        SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_W;
        (void)router.routeEvent(event, input, {});
        event.type = SDL_EVENT_KEY_UP;
        const auto result = router.routeEvent(event, input, {.bindingCapture = true});
        input.beginFrame();
        const auto routed = router.routeFrame(input, {});
        std::cout << "key released during capture: forwarded=" << result.forwardedToInput
                  << " key still down=" << input.keyDown(SDL_SCANCODE_W)
                  << " gameplay up held=" << routed.gameplay.up.down << '\n';
    }
    {
        try {
        const auto mesh = loadGltfMesh(fixtures/"nonuniform.gltf");
        const auto e = mesh.vertices[1].position - mesh.vertices[0].position;
        const auto n = mesh.vertices[0].normal;
        const float error = e.x*n.x+e.y*n.y+e.z*n.z;
        std::cout << "normalized mesh normal dot surface edge (expected 0)=" << error << '\n';
        } catch(const std::exception& e) { std::cout << "normal fixture error=" << e.what() << '\n'; }
    }
    {
        const auto source = root / "import-source";
        const auto runtime = root / "import-runtime";
        fs::create_directories(source); fs::create_directories(runtime);
        fs::copy_file("assets/manifest.json", source/"manifest.json");
        fs::copy_file(fixtures/"external.glb", source/"external.glb");
        fs::copy_file(fixtures/"triangle.bin", source/"triangle.bin");
        const auto sourceMesh = loadGltfMesh(source/"external.glb");
        std::cout << "source GLB loads: vertices=" << sourceMesh.vertices.size() << '\n';
        AssetManifestEditor editor; editor.initialize(source/"manifest.json");
        auto manifest = AssetManifest::loadFromFile(source/"manifest.json");
        const auto imported = DecorationAssetRegistry::registerMesh({source,runtime,"external.glb",manifest,editor});
        std::cout << "external-buffer GLB registration success=" << imported.succeeded
                  << " staged dependency=" << fs::exists(runtime/"triangle.bin") << '\n';
        try { (void)loadGltfMesh(runtime/"external.glb"); std::cout << "  mesh loaded\n"; }
        catch(const std::exception& e) { std::cout << "  load error=" << e.what() << '\n'; }
    }
    {
        const auto source = root / "level-source";
        const auto runtime = root / "level-runtime";
        const auto path = source/"level0/screen0.scr";
        fs::create_directories(path.parent_path());
        write(path,"@layer 0\n...\n@layer 1\nC  \n");
        fs::create_directories(runtime/"level0/screen0.scr");
        LevelEditor editor; editor.initialize(source,runtime,0,0);
        editor.setCell({2,0,1},TileType::Wall);
        const bool saved=editor.saveDocument(path);
        const auto changed=Level::loadFromFile(path);
        std::cout << "puzzle save with blocked mirror: returned=" << saved
                  << " source already changed=" << (changed.tileAt(2,0,1)==TileType::Wall)
                  << " message=" << editor.status() << '\n';
    }
    {
        const auto level=Level::loadFromLines({"@layer 0","....","@layer 1"," C  "},"probe");
        GameplaySession session; session.reset(level);
        for(int i=0;i<1000;++i) {
            session.queueMove(MoveDirection::Right);
            if(!session.tryStartNextAction(level,{})) return 4;
            session.advanceActiveAction(session.activeActionDuration()); session.completeActiveAction();
            session.queueUndo();
            if(!session.tryStartNextAction(level,{})) return 5;
            session.advanceActiveAction(session.activeActionDuration()); session.completeActiveAction();
        }
        std::cout << "after 1000 move/undo pairs: undo entries=" << session.undoCount()
                  << " completed actions=" << session.completedActionCount() << '\n';
    }
    {
        try {
        const auto source=root/"indexed-source";
        const auto runtime=root/"indexed-runtime";
        fs::create_directories(source/"level0");
        fs::create_directories(runtime/"levels/level0");
        write(source/"level0/screen0.scr","@layer 0\n...\n@layer 1\nC  \n");
        fs::copy_file(source/"level0/screen0.scr",runtime/"levels/level0/screen0.scr");
        write(runtime/"manifest.json","{}");
        const auto levelBytes=fs::file_size(runtime/"levels/level0/screen0.scr");
        write(runtime/"content.index","format 1\ngame-version review\nfile-count 2\ntotal-bytes "+std::to_string(levelBytes+2)+"\nfile 2 manifest.json\nfile "+std::to_string(levelBytes)+" levels/level0/screen0.scr\n");
        validateContentPackage(runtime,"review");
        LevelEditor editor; editor.initialize(source,runtime/"levels",0,0);
        editor.resizeDocument(4,1);
        editor.setCell({3,0,1},TileType::Wall);
        const bool saved=editor.saveDocument(source/"level0/screen0.scr");
        std::cout << "indexed package editor save: returned=" << saved;
        try { validateContentPackage(runtime,"review"); std::cout << " restart validation passed\n"; }
        catch(const std::exception& e) { std::cout << " restart rejected=" << e.what() << '\n'; }
        } catch(const std::exception& e) { std::cout << "index fixture error=" << e.what() << '\n'; }
    }
}

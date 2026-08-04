#!/usr/bin/env bash
# Build and run the test suites with g++ on Linux, without the full CMake build.
#
# Why this exists: the project's CMake configure hard-requires Vulkan and glslc
# for the renderer, so `cmake -S . -B build` cannot even configure on a machine
# without the Vulkan SDK. 41 of the 43 CTest suites do not need either. This
# compiles sokoban_core and sokoban_ui straight from the source lists in
# CMakeLists.txt and links each suite against them, so the whole headless suite
# is runnable in a plain Linux container.
#
# Only vulkan_device_selection and frame_descriptor_sync are left out; they
# include <vulkan/vulkan.h> and must be run from the Windows/MSVC build.
#
# Usage:
#   # once, if you want the 8 SDL-dependent suites too:
#   cmake -S third_party/SDL -B /tmp/sdlbuild -DCMAKE_BUILD_TYPE=Release \
#     -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
#     -DSDL_TEST_LIBRARY=OFF -DSDL_INSTALL=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF \
#     -DSDL_VULKAN=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_DBUS=OFF \
#     -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_PULSEAUDIO=OFF -DSDL_ALSA=OFF \
#     -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
#     -DSDL_UNIX_CONSOLE_BUILD=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
#   cmake --build /tmp/sdlbuild -j
#
#   tools/build_headless_tests.sh [output-dir]
#
# Object files and test binaries are incremental, so re-running after an edit
# rebuilds only what changed.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-/tmp/htest}"
JOBS="$(nproc)"

mkdir -p "$OUT/obj" "$OUT/bin" "$OUT/inc/nlohmann"
cp "$ROOT/third_party/nlohmann/json.hpp" "$OUT/inc/nlohmann/"

INCS=(-I"$ROOT/src" -I"$OUT/inc" -I"$ROOT/third_party" -I"$ROOT/third_party/stb" -I"$ROOT/third_party/SDL/include")
FLAGS=(-std=c++20 -O0 -g0 -Wall -Wextra -Wpedantic -DSOKOBAN_GAME_VERSION='"0.1.0"')

# SDL3 is built once from the vendored tree with the desktop backends off:
#   cmake -S third_party/SDL -B "$SDL_BUILD" -DSDL_STATIC=ON -DSDL_SHARED=OFF \
#     -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_VULKAN=OFF -DSDL_UNIX_CONSOLE_BUILD=ON ...
SDL_BUILD="${SDL_BUILD:-/tmp/sdlbuild}"
SDL_LIB="$SDL_BUILD/libSDL3.a"
if [ -f "$SDL_LIB" ]; then HAVE_SDL=1; else HAVE_SDL=0; echo "note: no $SDL_LIB - SDL suites skipped"; fi

# The real sokoban_core and sokoban_ui source lists, read from CMakeLists so
# they cannot drift. Application is excluded (composition root, pulls in
# Vulkan); everything else builds once SDL3 is available.
mapfile -t CORE < <(
  awk '/^add_library\(sokoban_core STATIC/{f=1;next} f&&/^\)/{f=0} f' "$ROOT/CMakeLists.txt" \
    | tr -d ' \r' | grep '\.cpp$' | grep -vE 'Application\.cpp$' | sed "s|^|$ROOT/|"
  if [ "$HAVE_SDL" = 1 ]; then
    awk '/^add_library\(sokoban_ui STATIC/{f=1;next} f&&/^\)/{f=0} f' "$ROOT/CMakeLists.txt" \
      | tr -d ' \r' | grep '\.cpp$' | sed "s|^|$ROOT/|"
  fi
)
if [ "$HAVE_SDL" = 1 ]; then
  INCS+=(-I"$SDL_BUILD/include-config-release" -I"$SDL_BUILD/include")
  LINKLIBS=("$SDL_LIB" -lpthread -ldl -lm)
else
  mapfile -t CORE < <(printf '%s\n' "${CORE[@]}" \
    | grep -vE '(Input|InputRouter|RuntimeContent|SaveStore|Window|AudioSystem|MiniaudioImpl)\.cpp$')
  LINKLIBS=(-lpthread)
fi
INCS+=(-I"$ROOT/third_party/miniaudio")

echo "== compiling ${#CORE[@]} core sources =="
: > "$OUT/units.txt"
for src in "${CORE[@]}"; do
  obj="$OUT/obj/$(echo "${src#"$ROOT/src/"}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  printf '%s\t%s\n' "$src" "$obj" >> "$OUT/units.txt"
done

# Incremental: an object newer than its source is reused. The sandbox kills
# background processes between calls, so this is run repeatedly until it
# reports every unit built.
todo=0
while IFS=$'\t' read -r src obj; do
  [ -f "$obj" ] && [ "$obj" -nt "$src" ] && continue
  todo=$((todo+1))
done < "$OUT/units.txt"
echo "== $todo units to build =="

running=0
while IFS=$'\t' read -r src obj; do
  [ -f "$obj" ] && [ "$obj" -nt "$src" ] && continue
  ( g++ "${FLAGS[@]}" "${INCS[@]}" -c "$src" -o "$obj" 2> "$obj.log" \
      || { echo "COMPILE FAIL: $src"; head -20 "$obj.log"; rm -f "$obj"; } ) &
  running=$((running+1))
  if [ "$running" -ge "$JOBS" ]; then wait -n 2>/dev/null || wait; running=$((running-1)); fi
done < "$OUT/units.txt"
wait

built=$(ls "$OUT"/obj/*.o 2>/dev/null | wc -l)
total=$(wc -l < "$OUT/units.txt")
if [ "$built" -lt "$total" ]; then
  echo "== INCOMPLETE: $built/$total objects; re-run this script =="
  exit 2
fi

rm -f "$OUT/libcore.a"
ar rcs "$OUT/libcore.a" "$OUT"/obj/*.o 2>/dev/null
echo "== core archive: $(ar t "$OUT/libcore.a" 2>/dev/null | wc -l) objects =="

# name:test-source. SDL-free suites.
TESTS="
rules:RulesTests
level:LevelTests
level_catalog:LevelCatalogTests
gameplay_session:GameplaySessionTests
gameplay_loop:GameplayLoopTests
campaign_session:CampaignSessionTests
settings_coordinator:SettingsCoordinatorTests
frame_resource_tracker:FrameResourceTrackerTests
renderer_reconfiguration:RendererReconfigurationTests
iso_scene_preparer:IsoScenePreparerTests
level_editor:LevelEditorTests
decoration_mesh_catalog:DecorationMeshCatalogTests
decoration_gizmo:DecorationGizmoTests
animation_controller:AnimationControllerTests
animation_catalog:AnimationCatalogTests
animation_event_sequencer:AnimationEventSequencerTests
asset_manifest:AssetManifestTests
asset_manifest_editor:AssetManifestEditorTests
presentation:PresentationTests
particles:ParticleSystemTests
asset_requirements:AssetRequirementsTests
tasks:TaskSystemTests
logging:LogTests
splat_canvas:SplatCanvasTests
splat_painter:SplatPainterTests
png_writer:PngWriterTests
ground_pick:GroundPickTests
action_plan:ActionPlanTests
action_scheduler:ActionSchedulerTests
reservation:ReservationTests
state_delta:StateDeltaTests
image_data:ImageDataTests
tile_thumbnail:TileThumbnailBakeTests
"
# Need SDL3 (SaveStore/Input/Window/RuntimeContent or sokoban_ui).
if [ "$HAVE_SDL" = 1 ]; then
  TESTS="$TESTS
player_profile:PlayerProfileTests
save_slots:SaveSlotManagerTests
input:InputTests
input_router:InputRouterTests
shell_flow:ShellFlowTests
ui:UiTests
title:TitleScreenTests
content_pipeline:ContentPipelineTests
"
fi
# vulkan_device_selection and frame_descriptor_sync need Vulkan headers, which
# cannot be installed here without root. Run those on Windows.

echo "== building test binaries =="
pending=0
running=0
for entry in $TESTS; do
  name="${entry%%:*}"; src="${entry##*:}"
  [ -f "$ROOT/tests/$src.cpp" ] || { echo "MISSING SOURCE: $src"; continue; }
  if [ -x "$OUT/bin/$name" ] && [ "$OUT/bin/$name" -nt "$ROOT/tests/$src.cpp" ] \
     && [ "$OUT/bin/$name" -nt "$OUT/libcore.a" ]; then continue; fi
  pending=$((pending+1))
  ( g++ "${FLAGS[@]}" "${INCS[@]}" -DSOKOBAN_TEST_ASSET_DIR="\"$ROOT/assets\"" \
      "$ROOT/tests/$src.cpp" "$OUT/libcore.a" "${LINKLIBS[@]}" -o "$OUT/bin/$name" \
      2> "$OUT/bin/$name.buildlog" \
      || { echo "LINK FAIL: $name"; head -12 "$OUT/bin/$name.buildlog"; rm -f "$OUT/bin/$name"; } ) &
  running=$((running+1))
  if [ "$running" -ge "$JOBS" ]; then wait -n 2>/dev/null || wait; running=$((running-1)); fi
done
wait
echo "== $pending test binaries (re)built =="

echo
echo "== running =="
pass=0; failn=0; skipped=""
for entry in $TESTS; do
  name="${entry%%:*}"
  if [ ! -x "$OUT/bin/$name" ]; then skipped="$skipped $name"; continue; fi
  if out=$(cd "$ROOT" && SOKOBAN_ASSETS="$ROOT/assets" "$OUT/bin/$name" 2>&1); then
    printf '  PASS  %-28s %s\n' "$name" "$(echo "$out" | tail -1)"; pass=$((pass+1))
  else
    printf '  FAIL  %-28s\n' "$name"; echo "$out" | tail -15 | sed 's/^/          /'; failn=$((failn+1))
  fi
done
echo
echo "passed=$pass failed=$failn"
[ -n "$skipped" ] && echo "not built:$skipped"

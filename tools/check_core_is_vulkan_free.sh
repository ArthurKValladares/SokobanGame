#!/usr/bin/env bash
# sokoban_core must not need Vulkan.
#
# CMake enforces this by omission: sokoban_core links SDL3 and the vendored
# third-party targets, and deliberately not Vulkan::Vulkan, so a core file that
# includes <vulkan/vulkan.h> - directly or through a header - fails to compile
# with "cannot open source file". That failure only appears in a real configure
# of that target, which means a compile check that hands every file every
# include path will not see it. One did not, and a Visual Studio build found
# the break instead.
#
# This compiles every sokoban_core source with the Vulkan include path
# deliberately absent, so the layering is checked rather than assumed. It needs
# only a C++20 compiler and the vendored headers; no Vulkan SDK, which is the
# point.
#
# Usage: tools/check_core_is_vulkan_free.sh [source-dir]
set -uo pipefail
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$root" || exit 1

# Everything sokoban_core is given - minus anything that could resolve
# vulkan/vulkan.h. SDL vendors a copy under src/video/khronos, so that path is
# excluded here even though the compile sweep uses it.
INC=(
  -I src
  -isystem third_party/SDL/include
  -isystem third_party/cgltf
  -isystem third_party/stb
  -isystem third_party/nlohmann
  -isystem third_party
  -isystem third_party/miniaudio
  -isystem third_party/bc7enc16
)
DEF=(
  -DSOKOBAN_ENABLE_DEBUG_UI=1
  -DSOKOBAN_ENABLE_TEST_HOOKS=1
  '-DSOKOBAN_GAME_VERSION="0.0.0"'
)
CXX="${CXX:-g++}"

sources=$(awk '
  /^add_library\(sokoban_core STATIC/ { inside = 1; next }
  inside && /^\)/                     { inside = 0 }
  inside && /^[ \t]+src\/.*\.cpp[ \t]*$/ {
    gsub(/[ \t]/, "", $0); print
  }
' CMakeLists.txt)

count=0
leaks=0
broken=0
while IFS= read -r source; do
  [ -n "$source" ] || continue
  count=$((count + 1))
  if ! output=$("$CXX" -std=c++20 -fsyntax-only \
      "${DEF[@]}" "${INC[@]}" "$source" 2>&1); then
    if printf '%s' "$output" | grep -q 'vulkan'; then
      echo "VULKAN LEAK  $source"
      printf '%s\n' "$output" | grep -m3 'vulkan'
      leaks=$((leaks + 1))
    else
      echo "DID NOT COMPILE  $source"
      printf '%s\n' "$output" | head -3
      broken=$((broken + 1))
    fi
  fi
done <<< "$sources"

# Every core source compiles with these flags in a healthy tree, so a failure
# for any other reason means this check is not checking what it claims -
# a missing compiler, the wrong working directory, a stale include list. That
# has to fail loudly. A check that reports success while compiling nothing is
# worse than no check.
if [ "$count" -eq 0 ]; then
  echo "Found no sokoban_core sources in CMakeLists.txt - the source list" \
       "could not be read, so nothing was checked."
  exit 1
fi

echo "checked $count sokoban_core sources; $leaks reach Vulkan," \
     "$broken failed to compile for other reasons"
[ "$leaks" -eq 0 ] && [ "$broken" -eq 0 ]

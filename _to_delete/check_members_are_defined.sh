#!/usr/bin/env bash
# A class's own members must be defined in its own translation unit.
#
# The compile sweep this review leaned on runs -fsyntax-only, so it never
# links, and a member that is *declared* in a header and defined nowhere is
# invisible to it. That is not hypothetical: moving publishGate() and
# recordPublishFailure() out of VulkanModelResources left one declaration
# behind, every call site bound to the member (which hides the free function
# by name lookup), and the first thing to notice was the MSVC linker.
#
# For each src/**/X.cpp with a matching X.hpp, this compiles the file and
# fails on any *undefined* symbol named sokoban::X::something - a member the
# class promises and its own object file does not deliver. Templates that are
# genuinely instantiated elsewhere would trip this; there are none, and if one
# appears it belongs in a header anyway, which is the same conclusion.
#
# Usage: tools/check_members_are_defined.sh [source-dir]
set -uo pipefail
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$root" || exit 1

INC=(
  -I src
  -isystem third_party/SDL/src/video/khronos
  -isystem third_party/SDL/include
  -isystem third_party/vma
  -isystem third_party/imgui
  -isystem third_party/imgui/backends
  -isystem third_party/imgui/misc/cpp
  -isystem third_party/cgltf
  -isystem third_party/stb
  -isystem third_party/nlohmann
  -isystem third_party
  -isystem third_party/miniaudio
  -isystem third_party/bc7enc16
)
DEF=(
  -DSOKOBAN_ENABLE_DEBUG_UI=1
  -DSOKOBAN_ENABLE_VALIDATION=1
  '-DSOKOBAN_GAME_VERSION="0.0.0"'
  '-DSOKOBAN_SOURCE_LEVEL_DIR="/l"'
  '-DSOKOBAN_SOURCE_ASSET_DIR="/a"'
)
CXX="${CXX:-g++}"
object=$(mktemp /tmp/member-check-XXXXXX.o)
trap 'rm -f "$object"' EXIT

checked=0
missing=0
broken=0
while IFS= read -r source; do
  header="${source%.cpp}.hpp"
  [ -f "$header" ] || continue
  class=$(basename "$source" .cpp)
  grep -qE "^(class|struct) ${class}\b" "$header" || continue
  checked=$((checked + 1))
  if ! output=$("$CXX" -std=c++20 -c -O0 "${DEF[@]}" "${INC[@]}" \
      "$source" -o "$object" 2>&1); then
    echo "DID NOT COMPILE  $source"
    printf '%s\n' "$output" | head -3
    broken=$((broken + 1))
    continue
  fi
  undefined=$(nm -C -u "$object" | grep -F "sokoban::${class}::" || true)
  if [ -n "$undefined" ]; then
    echo "UNDEFINED MEMBER  $source"
    printf '%s\n' "$undefined" | sed 's/^/    /'
    missing=$((missing + 1))
  fi
done < <(find src -name '*.cpp' | sort)

# A check that reports success while checking nothing is worse than no check.
if [ "$checked" -eq 0 ]; then
  echo "Found no class implementation files - nothing was checked."
  exit 1
fi
echo "$checked classes checked; $missing with undefined members, $broken did not compile."
[ "$missing" -eq 0 ] && [ "$broken" -eq 0 ]

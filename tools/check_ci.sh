#!/usr/bin/env bash
# Run the Linux CI configurations locally, retaining every compile failure.
set -euo pipefail
if [ "${1:-}" = --help ]; then
  echo "Usage: bash tools/check_ci.sh [ci-debug ci-dev-fast ci-release ci-tidy ci-sanitize headless-tests ci-fuzz]"
  echo "Defaults to all seven configurations; requires Linux, CI dependencies, and the pinned Vulkan SDK."
  exit 0
fi
if [ "$(uname -s)" != Linux ]; then
  echo "Linux CI checks require Linux (for example Ubuntu 24.04 in WSL). Windows checks cannot substitute for them." >&2
  exit 1
fi
cd "$(dirname "$0")/.."
presets=("$@")
if [ "${#presets[@]}" -eq 0 ]; then
  presets=(ci-debug ci-dev-fast ci-release ci-tidy ci-sanitize headless-tests ci-fuzz)
fi
needs_vulkan=false
for preset in "${presets[@]}"; do
  case "$preset" in
    ci-debug|ci-dev-fast|ci-release|ci-tidy|ci-sanitize|ci-fuzz) needs_vulkan=true ;;
    headless-tests) ;;
    *) echo "Unknown CI preset: $preset" >&2; exit 1 ;;
  esac
done
for program in cmake ninja gcc-13 g++-13 clang-18 clang++-18 clang-tidy-18 xvfb-run; do
  command -v "$program" >/dev/null || { echo "Missing CI dependency: $program" >&2; exit 1; }
done
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
if "$needs_vulkan"; then
  : "${VULKAN_SDK:?Expose the Vulkan SDK pinned in .github/workflows/required-tests.yml}"
  sdk_version=$(sed -n 's/^[[:space:]]*VULKAN_SDK_VERSION:[[:space:]]*//p' .github/workflows/required-tests.yml)
  header_version=$(sed -n 's/^#define VK_HEADER_VERSION //p' "$VULKAN_SDK/include/vulkan/vulkan_core.h")
  expected_header_version=$(printf '%s' "$sdk_version" | cut -d. -f3)
  if [ "$header_version" != "$expected_header_version" ]; then
    echo "Vulkan SDK headers differ from CI's $sdk_version; local validation would use different shader tools." >&2
    exit 1
  fi
  test -x "$VULKAN_SDK/bin/glslc" || { echo "SDK glslc is missing" >&2; exit 1; }
  export PATH="$VULKAN_SDK/bin:$PATH"
  export LD_LIBRARY_PATH="$VULKAN_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export CMAKE_PREFIX_PATH="$VULKAN_SDK:$VULKAN_SDK/lib/VulkanLoader"
  GITHUB_ENV="$scratch/vulkan.env" bash tools/configure_ci_vulkan.sh
  while IFS='=' read -r name value; do
    export "$name=$value"
  done < "$scratch/vulkan.env"
fi

run_preset() {
  local preset="$1"
  cmake --preset "$preset" || return
  cmake --build --preset "$preset" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" -- -k 0 || return
  case "$preset" in
    ci-debug|ci-dev-fast|ci-release|ci-sanitize|headless-tests)
      xvfb-run --auto-servernum ctest --preset "$preset" --timeout 60 || return ;;
    ci-fuzz)
      ./out/ci-fuzz/sokoban_player_profile_fuzz -max_len=65536 -max_total_time=60 -print_final_stats=1 || return ;;
  esac
  if [ "$preset" = ci-debug ]; then
    CXX=g++-13 bash tools/check_core_is_vulkan_free.sh || return
    xvfb-run --auto-servernum ./out/ci-debug/Debug/sokoban \
      --smoke-frames 240 --require-validation --save-directory "$scratch/profile" || return
  fi
}
failed=()
for preset in "${presets[@]}"; do
  if run_preset "$preset"; then
    echo "PASSED $preset"
  else
    failed+=("$preset")
    echo "FAILED $preset" >&2
  fi
done
if [ "${#failed[@]}" -ne 0 ]; then
  echo "Failed CI configurations: ${failed[*]}" >&2
  exit 1
fi

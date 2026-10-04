#!/usr/bin/env bash
# Share one software driver and validation-layer setup across CTest and rendering.
set -euo pipefail

icd_dir="${1:-/usr/share/vulkan/icd.d}"
system_layer_dir="${2:-/usr/share/vulkan/explicit_layer.d}"
driver_file=""
# Mesa packages have used both names. Prefer the native architecture when a
# runner also has a foreign-architecture driver installed.
for candidate in "${icd_dir}/lvp_icd.x86_64.json" "${icd_dir}/lvp_icd.json"; do
  if [ -f "${candidate}" ]; then
    driver_file="${candidate}"
    break
  fi
done
if [ -z "${driver_file}" ]; then
  echo "::error::lavapipe ICD manifest missing in ${icd_dir}; is mesa-vulkan-drivers installed?"
  exit 1
fi

# The SDK loader is first on LD_LIBRARY_PATH, so prefer its matching layers.
layer_dir="${VULKAN_SDK:-}/share/vulkan/explicit_layer.d"
if [ ! -f "${layer_dir}/VkLayer_khronos_validation.json" ]; then
  layer_dir="${system_layer_dir}"
fi
if [ ! -f "${layer_dir}/VkLayer_khronos_validation.json" ]; then
  echo "::error::VK_LAYER_KHRONOS_validation manifest not found"
  exit 1
fi

{
  echo "VK_DRIVER_FILES=${driver_file}"
  echo "VK_LAYER_PATH=${layer_dir}"
} >> "${GITHUB_ENV:?GITHUB_ENV must name the workflow environment file}"
echo "Using ICD ${driver_file} and layers ${layer_dir}"

cmake_minimum_required(VERSION 3.25)

project(Sokoban3D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Keep the ZIP self-contained instead of requiring the Visual C++
    # Redistributable to be installed separately on a player's machine.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Avoid leaking machine-specific checkout paths through __FILE__ strings
    # and make binaries produced from different workspaces more reproducible.
    add_compile_options(
        /experimental:deterministic
        "/pathmap:${CMAKE_SOURCE_DIR}=."
    )
endif()

option(SOKOBAN_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

add_library(sokoban_json INTERFACE)
target_include_directories(sokoban_json SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

add_library(sokoban_stb INTERFACE)
target_include_directories(sokoban_stb SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
    set(SDL_SHARED OFF CACHE BOOL "Build SDL3 as a shared library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build SDL3 as a static library" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build the SDL3_test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Build SDL3 tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL3 examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Install SDL3" FORCE)
    add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
else()
    find_package(SDL3 CONFIG REQUIRED)
endif()

set(SOKOBAN_IMGUI_SOURCES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    set(SOKOBAN_IMGUI_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl3.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/misc/cpp/imgui_stdlib.cpp"
    )
    if(MSVC)
        set_source_files_properties(${SOKOBAN_IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS /W0)
    endif()
endif()

find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(LEVEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/levels")

set(SHADER_SOURCES
    "${SHADER_SOURCE_DIR}/triangle.vert.glsl"
    "${SHADER_SOURCE_DIR}/triangle.frag.glsl"
    "${SHADER_SOURCE_DIR}/shadow.vert.glsl"
    "${SHADER_SOURCE_DIR}/model.vert.glsl"
    "${SHADER_SOURCE_DIR}/model_shadow.vert.glsl"
    "${SHADER_SOURCE_DIR}/fullscreen.vert.glsl"
    "${SHADER_SOURCE_DIR}/ssao.frag.glsl"
    "${SHADER_SOURCE_DIR}/ssao_composite.frag.glsl"
)

set(SHADER_OUTPUTS)
foreach(shader_source IN LISTS SHADER_SOURCES)
    get_filename_component(shader_name "${shader_source}" NAME)
    set(shader_output "${SHADER_OUTPUT_DIR}/${shader_name}.spv")
    if(shader_name MATCHES "\\.vert\\.")
        set(shader_stage vertex)
    elseif(shader_name MATCHES "\\.frag\\.")
        set(shader_stage fragment)
    else()
        message(FATAL_ERROR "Unknown shader stage for ${shader_name}")
    endif()
    list(APPEND SHADER_OUTPUTS "${shader_output}")
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
        # MODEL_TEXTURE_COUNT must equal sokoban::maxModelTextures in
        # src/engine/AssetManifest.hpp; descriptor writes pad to this size.
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.4 -DMODEL_TEXTURE_COUNT=16 -fshader-stage=${shader_stage} "${shader_source}" -o "${shader_output}"
        DEPENDS "${shader_source}"
        VERBATIM
    )
endforeach()

add_custom_target(sokoban_shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(sokoban_content_tool
    tools/ContentTool.cpp
    src/engine/AssetManifest.cpp
    src/engine/ContentPipeline.cpp
    src/engine/Level.cpp
    src/engine/TileTypes.cpp
)
target_include_directories(sokoban_content_tool PRIVATE src)
target_link_libraries(sokoban_content_tool PRIVATE sokoban_json)
set_target_properties(sokoban_content_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools/$<CONFIG>"
)
if(MSVC)
    target_compile_options(sokoban_content_tool PRIVATE /W4 /permissive-)
else()
    target_compile_options(sokoban_content_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(sokoban
    src/engine/AsyncSaveStore.cpp
    src/main.cpp
    src/engine/AnimationPreviewDebugUi.cpp
    src/engine/Application.cpp
    src/engine/ApplicationDebugUi.cpp
    src/engine/AssetManifest.cpp
    src/engine/AssetManifestDebugUi.cpp
    src/engine/AssetManifestEditor.cpp
    src/engine/AudioSystem.cpp
    src/engine/BoardLayout.cpp
    src/engine/DebugUi.cpp
    src/engine/GameplayPresentation.cpp
    src/engine/GameplaySession.cpp
    src/engine/Input.cpp
    src/engine/InputBindings.cpp
    src/engine/Level.cpp
    src/engine/LevelEditor.cpp
    src/engine/LevelEditorDebugUi.cpp
    src/engine/Log.cpp
    src/engine/MiniaudioImpl.cpp
    src/engine/PresentationSettings.cpp
    src/engine/PlayerProfile.cpp
    src/engine/PlayerProfileCodec.cpp
    src/engine/RenderFrameBuilder.cpp
    src/engine/Rules.cpp
    src/engine/RuntimeContent.cpp
    src/engine/SaveSlotManager.cpp
    src/engine/ShellFlow.cpp
    src/engine/SaveStore.cpp
    src/engine/TaskSystem.cpp
    src/engine/TileTypes.cpp
    src/engine/Window.cpp
    src/engine/render/VulkanRenderer.cpp
    src/engine/render/AnimationController.cpp
    src/engine/render/GltfMesh.cpp
    src/engine/render/ImageData.cpp
    src/engine/render/RenderAssetRequirements.cpp
    src/engine/render/RenderResolution.cpp
    src/engine/render/SkinnedMeshUpdater.cpp
    src/engine/render/VulkanDeviceSelection.cpp
    src/engine/render/VulkanModelResources.cpp
    src/engine/render/VulkanPipelineFactory.cpp
    src/engine/render/VulkanResourceUtils.cpp
    src/engine/render/VulkanSceneDescriptors.cpp
    src/engine/render/VulkanShadowPass.cpp
    src/engine/render/VulkanSsaoPass.cpp
    src/engine/render/VulkanSwapchainResources.cpp
    src/engine/render/VulkanUiResources.cpp
    src/engine/ui/FontAtlas.cpp
    src/engine/ui/LevelCompleteOverlay.cpp
    src/engine/ui/MenuKit.cpp
    src/engine/ui/OptionsMenu.cpp
    src/engine/ui/TitleScreen.cpp
    src/engine/ui/Ui.cpp
    src/engine/ui/UiControls.cpp
    src/engine/ui/UiLayout.cpp
)
foreach(imgui_source IN LISTS SOKOBAN_IMGUI_SOURCES)
    target_sources(sokoban PRIVATE $<$<CONFIG:Debug>:${imgui_source}>)
endforeach()

set_target_properties(sokoban PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
)

# Every build validates the manifest, its external glTF dependencies, playable
# levels, and compiled shaders before replacing the executable's content tree.
# The tool copies only reachable assets, keeping vendor packs out of packages.
add_custom_target(sokoban_content ALL
    COMMAND "$<TARGET_FILE:sokoban_content_tool>"
        --assets "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        --levels "${LEVEL_SOURCE_DIR}"
        --shaders "${SHADER_OUTPUT_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets"
        --version "${PROJECT_VERSION}"
    DEPENDS
        sokoban_content_tool
        sokoban_shaders
    VERBATIM
)
add_dependencies(sokoban sokoban_content)

target_include_directories(sokoban PRIVATE
    src
    third_party/imgui
    third_party/imgui/backends
    third_party/imgui/misc/cpp
    third_party/miniaudio
)
if(MSVC)
    set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS /W0)
else()
    set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS -w)
endif()
target_link_libraries(sokoban PRIVATE
    Vulkan::Vulkan
    Threads::Threads
    SDL3::SDL3
    sokoban_json
    sokoban_stb
)
if(UNIX AND NOT APPLE)
    # miniaudio dynamically loads native audio backends on Unix-like systems.
    target_link_libraries(sokoban PRIVATE ${CMAKE_DL_LIBS} m)
endif()

target_compile_definitions(sokoban PRIVATE
    SDL_MAIN_USE_CALLBACKS=0
    SOKOBAN_GAME_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Debug>:SOKOBAN_SOURCE_LEVEL_DIR="${LEVEL_SOURCE_DIR}">
    $<$<CONFIG:Debug>:SOKOBAN_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets">
    $<$<CONFIG:Debug>:SOKOBAN_ENABLE_DEBUG_UI=1>
)

if(SOKOBAN_ENABLE_VALIDATION)
    target_compile_definitions(sokoban PRIVATE SOKOBAN_ENABLE_VALIDATION=1)
else()
    target_compile_definitions(sokoban PRIVATE SOKOBAN_ENABLE_VALIDATION=0)
endif()

if(MSVC)
    target_compile_options(sokoban PRIVATE /W4 /permissive-)
else()
    target_compile_options(sokoban PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(SOKOBAN_BUILD_TESTS "Build headless engine tests" ON)

# Declares one headless test executable with the project's include path and
# warning set, registered with CTest.
#   sokoban_add_test(
#       NAME <ctest name> TARGET <executable name>
#       SOURCES <files...>
#       [LIBS <libraries...>]
#       [ASSET_DIR]   # compiles with SOKOBAN_TEST_ASSET_DIR = source assets
#       [ASSETS_ENV]  # runs with SOKOBAN_ASSETS env var = source assets
#   )
function(sokoban_add_test)
    cmake_parse_arguments(ARG "ASSET_DIR;ASSETS_ENV" "NAME;TARGET" "SOURCES;LIBS" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_TARGET OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sokoban_add_test requires NAME, TARGET, and SOURCES")
    endif()
    add_executable(${ARG_TARGET} ${ARG_SOURCES})
    target_include_directories(${ARG_TARGET} PRIVATE src)
    if(ARG_LIBS)
        target_link_libraries(${ARG_TARGET} PRIVATE ${ARG_LIBS})
    endif()
    if(ARG_ASSET_DIR)
        target_compile_definitions(${ARG_TARGET} PRIVATE
            SOKOBAN_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endif()
    if(MSVC)
        target_compile_options(${ARG_TARGET} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${ARG_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${ARG_NAME} COMMAND ${ARG_TARGET})
    if(ARG_ASSETS_ENV)
        set_tests_properties(${ARG_NAME} PROPERTIES
            ENVIRONMENT "SOKOBAN_ASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endif()
endfunction()

if(SOKOBAN_BUILD_TESTS)
    enable_testing()

    sokoban_add_test(NAME rules TARGET sokoban_rules_tests
        SOURCES
            tests/RulesTests.cpp
            src/engine/Level.cpp
            src/engine/Rules.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME level TARGET sokoban_level_tests
        SOURCES
            tests/LevelTests.cpp
            src/engine/Level.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME gameplay_session TARGET sokoban_gameplay_session_tests
        SOURCES
            tests/GameplaySessionTests.cpp
            src/engine/GameplaySession.cpp
            src/engine/Level.cpp
            src/engine/Rules.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME player_profile TARGET sokoban_profile_tests
        LIBS sokoban_json SDL3::SDL3
        SOURCES
            tests/PlayerProfileTests.cpp
            src/engine/AsyncSaveStore.cpp
            src/engine/InputBindings.cpp
            src/engine/PlayerProfile.cpp
            src/engine/PlayerProfileCodec.cpp
            src/engine/render/RenderResolution.cpp
            src/engine/SaveStore.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME save_slots TARGET sokoban_save_slot_tests
        LIBS sokoban_json SDL3::SDL3
        SOURCES
            tests/SaveSlotManagerTests.cpp
            src/engine/AsyncSaveStore.cpp
            src/engine/InputBindings.cpp
            src/engine/PlayerProfile.cpp
            src/engine/PlayerProfileCodec.cpp
            src/engine/render/RenderResolution.cpp
            src/engine/SaveSlotManager.cpp
            src/engine/SaveStore.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME shell_flow TARGET sokoban_shell_flow_tests
        SOURCES
            tests/ShellFlowTests.cpp
            src/engine/InputBindings.cpp
            src/engine/ShellFlow.cpp
    )
    sokoban_add_test(NAME input TARGET sokoban_input_tests
        LIBS SDL3::SDL3
        SOURCES
            tests/InputTests.cpp
            src/engine/Input.cpp
            src/engine/InputBindings.cpp
    )
    sokoban_add_test(NAME image_data TARGET sokoban_image_data_tests
        LIBS Threads::Threads sokoban_stb
        ASSET_DIR
        SOURCES
            tests/ImageDataTests.cpp
            src/engine/render/ImageData.cpp
    )
    sokoban_add_test(NAME ui TARGET sokoban_ui_tests
        LIBS sokoban_stb
        ASSET_DIR
        SOURCES
            tests/UiTests.cpp
            src/engine/InputBindings.cpp
            src/engine/ui/FontAtlas.cpp
            src/engine/ui/MenuKit.cpp
            src/engine/ui/OptionsMenu.cpp
            src/engine/ui/Ui.cpp
            src/engine/ui/UiControls.cpp
            src/engine/ui/UiLayout.cpp
            src/engine/render/RenderResolution.cpp
    )
    sokoban_add_test(NAME title TARGET sokoban_title_tests
        LIBS sokoban_stb
        ASSET_DIR
        SOURCES
            tests/TitleScreenTests.cpp
            src/engine/InputBindings.cpp
            src/engine/ui/FontAtlas.cpp
            src/engine/ui/LevelCompleteOverlay.cpp
            src/engine/ui/MenuKit.cpp
            src/engine/ui/OptionsMenu.cpp
            src/engine/ui/TitleScreen.cpp
            src/engine/ui/Ui.cpp
            src/engine/ui/UiControls.cpp
            src/engine/ui/UiLayout.cpp
            src/engine/render/RenderResolution.cpp
    )
    sokoban_add_test(NAME vulkan_device_selection TARGET sokoban_vulkan_device_selection_tests
        LIBS Vulkan::Vulkan
        SOURCES
            tests/VulkanDeviceSelectionTests.cpp
            src/engine/render/VulkanDeviceSelection.cpp
            src/engine/render/RenderResolution.cpp
    )
    sokoban_add_test(NAME level_editor TARGET sokoban_level_editor_tests
        SOURCES
            tests/LevelEditorTests.cpp
            src/engine/LevelEditor.cpp
            src/engine/Level.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME animation_controller TARGET sokoban_animation_controller_tests
        SOURCES
            tests/AnimationControllerTests.cpp
            src/engine/render/AnimationController.cpp
    )
    sokoban_add_test(NAME asset_manifest TARGET sokoban_asset_manifest_tests
        LIBS sokoban_json
        ASSETS_ENV
        SOURCES
            tests/AssetManifestTests.cpp
            src/engine/AssetManifest.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME asset_manifest_editor TARGET sokoban_asset_manifest_editor_tests
        LIBS sokoban_json
        ASSETS_ENV
        SOURCES
            tests/AssetManifestEditorTests.cpp
            src/engine/AssetManifest.cpp
            src/engine/AssetManifestEditor.cpp
            src/engine/TileTypes.cpp
    )
    sokoban_add_test(NAME presentation TARGET sokoban_presentation_tests
        LIBS sokoban_json
        SOURCES
            tests/PresentationTests.cpp
            src/engine/AssetManifest.cpp
            src/engine/GameplayPresentation.cpp
            src/engine/Level.cpp
            src/engine/LevelEditor.cpp
            src/engine/PresentationSettings.cpp
            src/engine/RenderFrameBuilder.cpp
            src/engine/Rules.cpp
            src/engine/TileTypes.cpp
            src/engine/render/RenderAssetRequirements.cpp
    )
    sokoban_add_test(NAME asset_requirements TARGET sokoban_asset_requirements_tests
        LIBS sokoban_json
        SOURCES
            tests/AssetRequirementsTests.cpp
            src/engine/AssetManifest.cpp
            src/engine/Level.cpp
            src/engine/TileTypes.cpp
            src/engine/render/RenderAssetRequirements.cpp
    )
    sokoban_add_test(NAME tasks TARGET sokoban_task_tests
        LIBS Threads::Threads
        SOURCES
            tests/TaskSystemTests.cpp
            src/engine/TaskSystem.cpp
    )
    sokoban_add_test(NAME content_pipeline TARGET sokoban_content_pipeline_tests
        LIBS sokoban_json
        SOURCES
            tests/ContentPipelineTests.cpp
            src/engine/AssetManifest.cpp
            src/engine/ContentPipeline.cpp
            src/engine/Level.cpp
            src/engine/TileTypes.cpp
    )
endif()

install(TARGETS sokoban RUNTIME DESTINATION . COMPONENT Runtime)
install(
    DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/"
    DESTINATION assets
    COMPONENT Runtime
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/LICENSE.txt"
    DESTINATION licenses
    RENAME SDL-LICENSE.txt
    COMPONENT Runtime
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/LICENSE"
    DESTINATION licenses
    RENAME miniaudio-LICENSE.txt
    COMPONENT Runtime
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/LICENSE.MIT"
    DESTINATION licenses
    RENAME nlohmann-json-LICENSE.txt
    COMPONENT Runtime
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/LICENSE"
    DESTINATION licenses
    RENAME stb-LICENSE.txt
    COMPONENT Runtime
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/LICENSE.txt")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/LICENSE.txt"
        DESTINATION licenses
        RENAME imgui-LICENSE.txt
        COMPONENT Runtime
    )
endif()

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "Sokoban3D")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
if(CMAKE_GENERATOR_PLATFORM)
    set(SOKOBAN_PACKAGE_ARCHITECTURE "${CMAKE_GENERATOR_PLATFORM}")
elseif(CMAKE_SYSTEM_PROCESSOR)
    set(SOKOBAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(SOKOBAN_PACKAGE_ARCHITECTURE "unknown")
endif()
string(TOLOWER "${SOKOBAN_PACKAGE_ARCHITECTURE}" SOKOBAN_PACKAGE_ARCHITECTURE_LOWER)
if(SOKOBAN_PACKAGE_ARCHITECTURE_LOWER MATCHES "^(amd64|x86_64)$")
    set(SOKOBAN_PACKAGE_ARCHITECTURE "x64")
elseif(SOKOBAN_PACKAGE_ARCHITECTURE_LOWER MATCHES "^(aarch64|arm64)$")
    set(SOKOBAN_PACKAGE_ARCHITECTURE "arm64")
endif()
string(REGEX REPLACE "[^A-Za-z0-9_.-]" "_"
    SOKOBAN_PACKAGE_ARCHITECTURE "${SOKOBAN_PACKAGE_ARCHITECTURE}")
set(CPACK_PACKAGE_FILE_NAME
    "Sokoban3D-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${SOKOBAN_PACKAGE_ARCHITECTURE}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)

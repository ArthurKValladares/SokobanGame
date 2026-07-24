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
    "${SHADER_SOURCE_DIR}/water.frag.glsl"
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
)
target_include_directories(sokoban_content_tool PRIVATE src)
set_target_properties(sokoban_content_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools/$<CONFIG>"
)
if(MSVC)
    target_compile_options(sokoban_content_tool PRIVATE /W4 /permissive-)
else()
    target_compile_options(sokoban_content_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(sokoban_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(sokoban_core STATIC
    src/engine/AtomicFile.cpp
    src/engine/AsyncSaveStore.cpp
    src/engine/AssetManifest.cpp
    src/engine/AssetManifestEditor.cpp
    src/engine/AudioSystem.cpp
    src/engine/CampaignSession.cpp
    src/engine/BoardLayout.cpp
    src/engine/ContentPipeline.cpp
    src/engine/GameplayPresentation.cpp
    src/engine/GameplayLoop.cpp
    src/engine/GameplaySession.cpp
    src/engine/Input.cpp
    src/engine/InputBindings.cpp
    src/engine/Level.cpp
    src/engine/LevelCatalog.cpp
    src/engine/LevelEditor.cpp
    src/engine/LevelProjectStore.cpp
    src/engine/Log.cpp
    src/engine/LogQueue.cpp
    src/engine/MiniaudioImpl.cpp
    src/engine/PresentationSettings.cpp
    src/engine/PlayerProfile.cpp
    src/engine/PlayerProfileCodec.cpp
    src/engine/RenderFrameBuilder.cpp
    src/engine/Rules.cpp
    src/engine/RuntimeContent.cpp
    src/engine/SaveSlotManager.cpp
    src/engine/SaveStore.cpp
    src/engine/SettingsCoordinator.cpp
    src/engine/SettingsTypes.cpp
    src/engine/TaskSystem.cpp
    src/engine/TileTypes.cpp
    src/engine/Window.cpp
    src/engine/render/AnimationController.cpp
    src/engine/render/GltfMesh.cpp
    src/engine/render/FrameResourceTracker.cpp
    src/engine/render/ImageData.cpp
    src/engine/render/RenderAssetRequirements.cpp
    src/engine/render/RenderResolution.cpp
    src/engine/render/RendererReconfiguration.cpp
    src/engine/render/IsoScenePreparer.cpp
)
add_library(Sokoban::core ALIAS sokoban_core)
target_include_directories(sokoban_core PUBLIC src)
target_include_directories(sokoban_core PRIVATE third_party/miniaudio)
target_link_libraries(sokoban_core
    PUBLIC SDL3::SDL3
    PRIVATE Threads::Threads sokoban_json sokoban_stb
)
target_compile_definitions(sokoban_core PRIVATE
    SOKOBAN_GAME_VERSION="${PROJECT_VERSION}"
)
if(UNIX AND NOT APPLE)
    # miniaudio dynamically loads native audio backends on Unix-like systems.
    target_link_libraries(sokoban_core PRIVATE ${CMAKE_DL_LIBS} m)
endif()
sokoban_enable_warnings(sokoban_core)

add_library(sokoban_ui STATIC
    src/engine/InputRouter.cpp
    src/engine/ShellFlow.cpp
    src/engine/ui/FontAtlas.cpp
    src/engine/ui/LevelCompleteOverlay.cpp
    src/engine/ui/MenuKit.cpp
    src/engine/ui/OptionsMenu.cpp
    src/engine/ui/TitleScreen.cpp
    src/engine/ui/Ui.cpp
    src/engine/ui/UiControls.cpp
    src/engine/ui/UiLayout.cpp
)
add_library(Sokoban::ui ALIAS sokoban_ui)
target_include_directories(sokoban_ui PUBLIC src)
target_link_libraries(sokoban_ui
    PUBLIC sokoban_core
    PRIVATE sokoban_stb
)
sokoban_enable_warnings(sokoban_ui)

add_library(sokoban_render_vulkan STATIC
    src/engine/render/VulkanRenderer.cpp
    src/engine/render/SkinnedMeshUpdater.cpp
    src/engine/render/VulkanDeviceContext.cpp
    src/engine/render/VulkanDeviceSelection.cpp
    src/engine/render/VulkanModelResources.cpp
    src/engine/render/VulkanPipelineFactory.cpp
    src/engine/render/VulkanResourceUtils.cpp
    src/engine/render/VulkanSceneRecorder.cpp
    src/engine/render/VulkanSceneDescriptors.cpp
    src/engine/render/VulkanShadowPass.cpp
    src/engine/render/VulkanSsaoPass.cpp
    src/engine/render/VulkanSwapchainResources.cpp
    src/engine/render/VulkanUiResources.cpp
)
add_library(Sokoban::render_vulkan ALIAS sokoban_render_vulkan)
foreach(imgui_source IN LISTS SOKOBAN_IMGUI_SOURCES)
    target_sources(sokoban_render_vulkan PRIVATE $<$<CONFIG:Debug>:${imgui_source}>)
endforeach()
target_include_directories(sokoban_render_vulkan
    PUBLIC src
    PRIVATE
        third_party/imgui
        third_party/imgui/backends
        third_party/imgui/misc/cpp
)
target_link_libraries(sokoban_render_vulkan
    PUBLIC
        sokoban_core
        sokoban_ui
        Vulkan::Vulkan
        SDL3::SDL3
)
target_compile_definitions(sokoban_render_vulkan PRIVATE
    $<$<CONFIG:Debug>:SOKOBAN_ENABLE_DEBUG_UI=1>
)
if(SOKOBAN_ENABLE_VALIDATION)
    target_compile_definitions(
        sokoban_render_vulkan PRIVATE SOKOBAN_ENABLE_VALIDATION=1)
else()
    target_compile_definitions(
        sokoban_render_vulkan PRIVATE SOKOBAN_ENABLE_VALIDATION=0)
endif()
sokoban_enable_warnings(sokoban_render_vulkan)

target_link_libraries(sokoban_content_tool PRIVATE sokoban_core)

add_executable(sokoban
    src/main.cpp
    src/engine/AnimationPreviewDebugUi.cpp
    src/engine/Application.cpp
    src/engine/ApplicationDebugUi.cpp
    src/engine/AssetManifestDebugUi.cpp
    src/engine/DebugUi.cpp
    src/engine/LevelEditorDebugUi.cpp
)

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
)
if(MSVC)
    set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS /W0)
else()
    set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS -w)
endif()
target_link_libraries(sokoban PRIVATE
    sokoban_core
    sokoban_ui
    sokoban_render_vulkan
)

target_compile_definitions(sokoban PRIVATE
    SDL_MAIN_USE_CALLBACKS=0
    SOKOBAN_GAME_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Debug>:SOKOBAN_SOURCE_LEVEL_DIR="${LEVEL_SOURCE_DIR}">
    $<$<CONFIG:Debug>:SOKOBAN_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets">
    $<$<CONFIG:Debug>:SOKOBAN_ENABLE_DEBUG_UI=1>
)

sokoban_enable_warnings(sokoban)

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
    foreach(test_source IN LISTS ARG_SOURCES)
        if(test_source MATCHES "(^|[/\\\\])src[/\\\\]")
            message(FATAL_ERROR
                "${ARG_TARGET} compiles production source ${test_source}; "
                "link its owning Sokoban library instead")
        endif()
    endforeach()
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
        LIBS sokoban_core
        SOURCES tests/RulesTests.cpp
    )
    sokoban_add_test(NAME level TARGET sokoban_level_tests
        LIBS sokoban_core
        SOURCES tests/LevelTests.cpp
    )
    sokoban_add_test(NAME level_catalog TARGET sokoban_level_catalog_tests
        LIBS sokoban_core
        SOURCES tests/LevelCatalogTests.cpp
    )
    sokoban_add_test(NAME gameplay_session TARGET sokoban_gameplay_session_tests
        LIBS sokoban_core
        SOURCES tests/GameplaySessionTests.cpp
    )
    sokoban_add_test(NAME player_profile TARGET sokoban_profile_tests
        LIBS sokoban_core sokoban_json
        SOURCES tests/PlayerProfileTests.cpp
    )
    sokoban_add_test(NAME save_slots TARGET sokoban_save_slot_tests
        LIBS sokoban_core sokoban_json
        SOURCES tests/SaveSlotManagerTests.cpp
    )
    sokoban_add_test(NAME shell_flow TARGET sokoban_shell_flow_tests
        LIBS sokoban_ui
        SOURCES tests/ShellFlowTests.cpp
    )
    sokoban_add_test(NAME input TARGET sokoban_input_tests
        LIBS sokoban_core
        SOURCES tests/InputTests.cpp
    )
    sokoban_add_test(NAME image_data TARGET sokoban_image_data_tests
        LIBS sokoban_core
        ASSET_DIR
        SOURCES tests/ImageDataTests.cpp
    )
    sokoban_add_test(NAME ui TARGET sokoban_ui_tests
        LIBS sokoban_ui
        ASSET_DIR
        SOURCES tests/UiTests.cpp
    )
    sokoban_add_test(NAME title TARGET sokoban_title_tests
        LIBS sokoban_ui
        ASSET_DIR
        SOURCES tests/TitleScreenTests.cpp
    )
    sokoban_add_test(NAME vulkan_device_selection TARGET sokoban_vulkan_device_selection_tests
        LIBS sokoban_render_vulkan
        SOURCES tests/VulkanDeviceSelectionTests.cpp
    )
    sokoban_add_test(NAME input_router TARGET sokoban_input_router_tests
        LIBS sokoban_ui
        SOURCES tests/InputRouterTests.cpp
    )
    sokoban_add_test(NAME settings_coordinator TARGET sokoban_settings_coordinator_tests
        LIBS sokoban_core
        SOURCES tests/SettingsCoordinatorTests.cpp
    )
    sokoban_add_test(NAME campaign_session TARGET sokoban_campaign_session_tests
        LIBS sokoban_core
        SOURCES tests/CampaignSessionTests.cpp
    )
    sokoban_add_test(NAME gameplay_loop TARGET sokoban_gameplay_loop_tests
        LIBS sokoban_core
        SOURCES tests/GameplayLoopTests.cpp
    )
    sokoban_add_test(NAME frame_descriptor_sync TARGET sokoban_frame_descriptor_sync_tests
        LIBS sokoban_render_vulkan
        SOURCES tests/FrameDescriptorSyncTests.cpp
    )
    sokoban_add_test(NAME frame_resource_tracker TARGET sokoban_frame_resource_tracker_tests
        LIBS sokoban_core
        SOURCES tests/FrameResourceTrackerTests.cpp
    )
    sokoban_add_test(NAME renderer_reconfiguration TARGET sokoban_renderer_reconfiguration_tests
        LIBS sokoban_core
        SOURCES tests/RendererReconfigurationTests.cpp
    )
    sokoban_add_test(NAME iso_scene_preparer TARGET sokoban_iso_scene_preparer_tests
        LIBS sokoban_core
        SOURCES tests/IsoScenePreparerTests.cpp
    )
    sokoban_add_test(NAME level_editor TARGET sokoban_level_editor_tests
        LIBS sokoban_core
        SOURCES tests/LevelEditorTests.cpp
    )
    sokoban_add_test(NAME animation_controller TARGET sokoban_animation_controller_tests
        LIBS sokoban_core
        SOURCES tests/AnimationControllerTests.cpp
    )
    sokoban_add_test(NAME asset_manifest TARGET sokoban_asset_manifest_tests
        LIBS sokoban_core sokoban_json
        ASSETS_ENV
        SOURCES tests/AssetManifestTests.cpp
    )
    sokoban_add_test(NAME asset_manifest_editor TARGET sokoban_asset_manifest_editor_tests
        LIBS sokoban_core
        ASSETS_ENV
        SOURCES tests/AssetManifestEditorTests.cpp
    )
    sokoban_add_test(NAME presentation TARGET sokoban_presentation_tests
        LIBS sokoban_core
        SOURCES tests/PresentationTests.cpp
    )
    sokoban_add_test(NAME asset_requirements TARGET sokoban_asset_requirements_tests
        LIBS sokoban_core
        SOURCES tests/AssetRequirementsTests.cpp
    )
    sokoban_add_test(NAME tasks TARGET sokoban_task_tests
        LIBS sokoban_core
        SOURCES tests/TaskSystemTests.cpp
    )
    sokoban_add_test(NAME logging TARGET sokoban_log_tests
        LIBS sokoban_core
        SOURCES tests/LogTests.cpp
    )
    sokoban_add_test(NAME content_pipeline TARGET sokoban_content_pipeline_tests
        LIBS sokoban_core
        SOURCES tests/ContentPipelineTests.cpp
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

cmake_minimum_required(VERSION 3.25)

project(Sokoban3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SOKOBAN_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

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
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders")
set(LEVEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/levels")
set(LEVEL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets/levels")

include(cmake/AssetManifest.cmake)

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
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.4 -DMODEL_TEXTURE_COUNT=${SOKOBAN_MODEL_TEXTURE_COUNT} -fshader-stage=${shader_stage} "${shader_source}" -o "${shader_output}"
        DEPENDS "${shader_source}"
        VERBATIM
    )
endforeach()

add_custom_target(sokoban_shaders DEPENDS ${SHADER_OUTPUTS})

file(GLOB_RECURSE LEVEL_SOURCES CONFIGURE_DEPENDS "${LEVEL_SOURCE_DIR}/*")

add_custom_target(sokoban_assets
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${LEVEL_SOURCE_DIR}" "${LEVEL_OUTPUT_DIR}"
    ${SOKOBAN_ASSET_COPY_COMMANDS}
    DEPENDS
        ${LEVEL_SOURCES}
        ${SOKOBAN_ASSET_DEPENDENCIES}
    VERBATIM
)

add_executable(sokoban
    src/main.cpp
    src/engine/AnimationPreviewDebugUi.cpp
    src/engine/Application.cpp
    src/engine/ApplicationDebugUi.cpp
    src/engine/BoardLayout.cpp
    src/engine/DebugUi.cpp
    src/engine/GameplayPresentation.cpp
    src/engine/GameplaySession.cpp
    src/engine/Input.cpp
    src/engine/Level.cpp
    src/engine/LevelEditor.cpp
    src/engine/LevelEditorDebugUi.cpp
    src/engine/PresentationSettings.cpp
    src/engine/RenderFrameBuilder.cpp
    src/engine/Rules.cpp
    src/engine/TaskSystem.cpp
    src/engine/TileTypes.cpp
    src/engine/Window.cpp
    src/engine/render/VulkanRenderer.cpp
    src/engine/render/AnimationController.cpp
    src/engine/render/GltfMesh.cpp
    src/engine/render/ImageData.cpp
    src/engine/render/RenderAssetRequirements.cpp
    src/engine/render/SkinnedMeshUpdater.cpp
    src/engine/render/VulkanModelResources.cpp
    src/engine/render/VulkanPipelineFactory.cpp
    src/engine/render/VulkanResourceUtils.cpp
    src/engine/render/VulkanSceneDescriptors.cpp
    src/engine/render/VulkanShadowPass.cpp
    src/engine/render/VulkanSsaoPass.cpp
    src/engine/render/VulkanSwapchainResources.cpp
    src/engine/ui/Ui.cpp
)
foreach(imgui_source IN LISTS SOKOBAN_IMGUI_SOURCES)
    target_sources(sokoban PRIVATE $<$<CONFIG:Debug>:${imgui_source}>)
endforeach()

add_dependencies(sokoban sokoban_shaders sokoban_assets)

target_include_directories(sokoban PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    third_party/imgui
    third_party/imgui/backends
    third_party/imgui/misc/cpp
)
target_link_libraries(sokoban PRIVATE
    Vulkan::Vulkan
    Threads::Threads
    SDL3::SDL3
    ole32
    windowscodecs
)

target_compile_definitions(sokoban PRIVATE
    SDL_MAIN_USE_CALLBACKS=0
    SOKOBAN_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}/assets"
    SOKOBAN_SOURCE_LEVEL_DIR="${LEVEL_SOURCE_DIR}"
    SOKOBAN_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
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
if(SOKOBAN_BUILD_TESTS)
    enable_testing()
    add_executable(sokoban_rules_tests
        tests/RulesTests.cpp
        src/engine/Level.cpp
        src/engine/Rules.cpp
        src/engine/TileTypes.cpp
    )
    target_include_directories(sokoban_rules_tests PRIVATE src)
    if(MSVC)
        target_compile_options(sokoban_rules_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_rules_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME rules COMMAND sokoban_rules_tests)

    add_executable(sokoban_level_tests
        tests/LevelTests.cpp
        src/engine/Level.cpp
        src/engine/TileTypes.cpp
    )
    target_include_directories(sokoban_level_tests PRIVATE src)
    if(MSVC)
        target_compile_options(sokoban_level_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_level_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME level COMMAND sokoban_level_tests)

    add_executable(sokoban_gameplay_session_tests
        tests/GameplaySessionTests.cpp
        src/engine/GameplaySession.cpp
        src/engine/Level.cpp
        src/engine/Rules.cpp
        src/engine/TileTypes.cpp
    )
    target_include_directories(sokoban_gameplay_session_tests PRIVATE src)
    if(MSVC)
        target_compile_options(sokoban_gameplay_session_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_gameplay_session_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gameplay_session COMMAND sokoban_gameplay_session_tests)

    add_executable(sokoban_level_editor_tests
        tests/LevelEditorTests.cpp
        src/engine/LevelEditor.cpp
        src/engine/Level.cpp
        src/engine/TileTypes.cpp
    )
    target_include_directories(sokoban_level_editor_tests PRIVATE src)
    if(MSVC)
        target_compile_options(sokoban_level_editor_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_level_editor_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME level_editor COMMAND sokoban_level_editor_tests)

    add_executable(sokoban_animation_controller_tests
        tests/AnimationControllerTests.cpp
        src/engine/render/AnimationController.cpp
    )
    target_include_directories(sokoban_animation_controller_tests PRIVATE src)
    if(MSVC)
        target_compile_options(sokoban_animation_controller_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_animation_controller_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME animation_controller COMMAND sokoban_animation_controller_tests)

    add_executable(sokoban_presentation_tests
        tests/PresentationTests.cpp
        src/engine/GameplayPresentation.cpp
        src/engine/Level.cpp
        src/engine/LevelEditor.cpp
        src/engine/PresentationSettings.cpp
        src/engine/RenderFrameBuilder.cpp
        src/engine/Rules.cpp
        src/engine/TileTypes.cpp
        src/engine/render/RenderAssetRequirements.cpp
    )
    target_include_directories(sokoban_presentation_tests PRIVATE src)
    if(MSVC)
        target_compile_options(sokoban_presentation_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_presentation_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME presentation COMMAND sokoban_presentation_tests)

    add_executable(sokoban_asset_requirements_tests
        tests/AssetRequirementsTests.cpp
        src/engine/Level.cpp
        src/engine/TileTypes.cpp
        src/engine/render/RenderAssetRequirements.cpp
    )
    target_include_directories(sokoban_asset_requirements_tests PRIVATE src)
    if(MSVC)
        target_compile_options(sokoban_asset_requirements_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_asset_requirements_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME asset_requirements COMMAND sokoban_asset_requirements_tests)

    add_executable(sokoban_task_tests
        tests/TaskSystemTests.cpp
        src/engine/TaskSystem.cpp
    )
    target_include_directories(sokoban_task_tests PRIVATE src)
    target_link_libraries(sokoban_task_tests PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(sokoban_task_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sokoban_task_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME tasks COMMAND sokoban_task_tests)
endif()

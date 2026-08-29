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
        # CMake/MSBuild may compile a static library with /MP. /FS prevents
        # concurrent compiler instances from racing the shared Debug PDB.
        /FS
        "/pathmap:${CMAKE_SOURCE_DIR}=."
    )
endif()

option(SOKOBAN_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)
option(SOKOBAN_SHIPPING_BUILD
    "Configure a player-facing, editor-free release package" OFF)
option(SOKOBAN_ENABLE_DEVELOPER_TOOLS
    "Compile the Debug-only ImGui workspace and content-editing tools" ON)
option(SOKOBAN_ENABLE_LTO
    "Enable interprocedural optimization for Release targets" OFF)
option(SOKOBAN_INSTALL_EXTERNAL_SYMBOLS
    "Install MSVC Program Database files in a separate symbols component" OFF)
option(SOKOBAN_ENABLE_SANITIZERS
    "Instrument Sokoban targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SOKOBAN_ENABLE_CLANG_TIDY
    "Run the selected clang-tidy checks while compiling Sokoban targets" OFF)
option(SOKOBAN_BUILD_FUZZ_TESTS
    "Build libFuzzer harnesses for hostile serialized input" OFF)
option(SOKOBAN_BUILD_INSTALLER
    "Build a per-user Inno Setup installer from the shipping runtime component" OFF)
option(SOKOBAN_SIGN_ARTIFACTS
    "Sign Windows release artifacts using a current-user certificate" OFF)
set(SOKOBAN_APP_PUBLISHER "Sokoban 3D" CACHE STRING
    "Publisher shown in Windows metadata and the installer")
set(SOKOBAN_SIGNING_CERTIFICATE_THUMBPRINT "" CACHE STRING
    "SHA-1 thumbprint of the current-user code-signing certificate")
set(SOKOBAN_SIGNING_TIMESTAMP_URL "http://timestamp.digicert.com" CACHE STRING
    "RFC 3161 timestamp URL passed to signtool")

if((SOKOBAN_BUILD_INSTALLER OR SOKOBAN_SIGN_ARTIFACTS) AND NOT WIN32)
    message(FATAL_ERROR
        "SOKOBAN_BUILD_INSTALLER and SOKOBAN_SIGN_ARTIFACTS are Windows-only")
endif()

if(SOKOBAN_SHIPPING_BUILD)
    if(NOT MSVC)
        message(FATAL_ERROR
            "SOKOBAN_SHIPPING_BUILD currently packages external symbols with MSVC")
    endif()
    # The shipping preset is intentionally opinionated. It creates only the
    # player-facing Release configuration; tests and all authoring code live
    # in their normal Debug/CI configurations instead.
    set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING
        "Supported build configurations" FORCE)
    set(SOKOBAN_ENABLE_VALIDATION OFF CACHE BOOL
        "Enable Vulkan validation layers in debug builds" FORCE)
    set(SOKOBAN_ENABLE_DEVELOPER_TOOLS OFF CACHE BOOL
        "Compile the Debug-only ImGui workspace and content-editing tools" FORCE)
    set(SOKOBAN_BUILD_TESTS OFF CACHE BOOL
        "Build headless engine tests" FORCE)
    set(SOKOBAN_BUILD_VULKAN_SMOKE_TESTS OFF CACHE BOOL
        "Build the hidden-window Vulkan device and submission smoke test" FORCE)
    set(SOKOBAN_ENABLE_LTO ON CACHE BOOL
        "Enable interprocedural optimization for Release targets" FORCE)
    set(SOKOBAN_INSTALL_EXTERNAL_SYMBOLS ON CACHE BOOL
        "Install MSVC Program Database files in a separate symbols component" FORCE)
endif()

if(MSVC AND SOKOBAN_INSTALL_EXTERNAL_SYMBOLS)
    # Keep full symbolic information out of the player package while still
    # producing a PDB that exactly matches the optimized executable.
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Release>:ProgramDatabase>"
        CACHE STRING "MSVC debug information format" FORCE)
    # Release PDB generation is shared by parallel compiler invocations in a
    # target. /FS serializes PDB access without giving up optimized symbols.
    add_compile_options("$<$<CONFIG:Release>:/FS>")
elseif(SOKOBAN_INSTALL_EXTERNAL_SYMBOLS)
    message(FATAL_ERROR
        "SOKOBAN_INSTALL_EXTERNAL_SYMBOLS currently requires MSVC PDB support")
endif()

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

# cgltf is vendored rather than fetched, like every other third-party header
# here, so a fresh clone that skipped it should say what to do rather than
# failing later with a missing include from the middle of GltfMesh.cpp.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf/cgltf.h")
    message(FATAL_ERROR
        "third_party/cgltf/cgltf.h is missing. Download cgltf.h and "
        "LICENSE.txt from https://github.com/jkuhlmann/cgltf (release v1.15, "
        "which is what this tree is written against) into third_party/cgltf/. "
        "Only the reader is needed; cgltf_write.h is not used.")
endif()
# SYSTEM, like sokoban_json and sokoban_stb: the implementation is compiled
# into GltfMesh.cpp by CGLTF_IMPLEMENTATION, which is how stb_image and
# stb_truetype are already handled, and this is what keeps 7,000 lines of
# vendored C out of /W4 and clang-tidy while leaving it inside ASan and UBSan
# - which is where a parser fed hostile files belongs.
add_library(sokoban_cgltf INTERFACE)
target_include_directories(sokoban_cgltf SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf"
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
    # Vendored sources must still compile, but they are outside the scope of
    # the project's lint policy. SKIP_LINTING is honored by CMake 3.27+ (the
    # version available on the CI runner) for clang-tidy and other linters.
    set_source_files_properties(${SOKOBAN_IMGUI_SOURCES} PROPERTIES SKIP_LINTING ON)
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

# Which shaders exist is sokoban::shaderCatalog's business, not this file's.
# Read the list back out of it rather than repeating it: the same list decides
# what gets compiled here, what ContentPipeline stages into the runtime asset
# tree, and what VulkanPipelineFactory loads, and a shader that reaches only
# some of those builds clean and then fails at launch. CMAKE_CONFIGURE_DEPENDS
# re-runs configure when the catalog changes, so a new shader is picked up
# without a manual reconfigure.
set(SHADER_CATALOG_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/ShaderCatalog.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SHADER_CATALOG_HEADER}")
file(READ "${SHADER_CATALOG_HEADER}" SHADER_CATALOG_TEXT)
string(REGEX MATCHALL "\"[A-Za-z0-9_]+\\.(vert|frag)\\.glsl\""
    SHADER_CATALOG_NAMES "${SHADER_CATALOG_TEXT}")
string(REGEX MATCH "std::array<std::string_view,[ \t\r\n]*([0-9]+)>[ \t\r\n]*sources"
    SHADER_CATALOG_SIZE_MATCH "${SHADER_CATALOG_TEXT}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR
        "Could not read the shader catalog size from ${SHADER_CATALOG_HEADER}.")
endif()
set(SHADER_CATALOG_SIZE ${CMAKE_MATCH_1})
list(LENGTH SHADER_CATALOG_NAMES SHADER_CATALOG_NAME_COUNT)
# A half-finished addition - a name without an array entry, or the reverse -
# is the exact mistake this catalog exists to prevent, so it fails configure
# rather than producing a build that is missing a shader.
if(NOT SHADER_CATALOG_NAME_COUNT EQUAL SHADER_CATALOG_SIZE)
    message(FATAL_ERROR
        "${SHADER_CATALOG_HEADER} spells ${SHADER_CATALOG_NAME_COUNT} shader "
        "filenames but declares sources as an array of ${SHADER_CATALOG_SIZE}. "
        "Every name needs a constant and an entry in sources, and nothing "
        "else in that header may quote a shader filename.")
endif()
set(SHADER_SOURCES)
foreach(quoted_shader_name IN LISTS SHADER_CATALOG_NAMES)
    string(REPLACE "\"" "" shader_catalog_name "${quoted_shader_name}")
    set(shader_catalog_path "${SHADER_SOURCE_DIR}/${shader_catalog_name}")
    # Say which file is missing and where it was named. Left to the custom
    # command's DEPENDS, this surfaces as "No rule to make target" from the
    # generator, which does not point at the catalog.
    if(NOT EXISTS "${shader_catalog_path}")
        message(FATAL_ERROR
            "${SHADER_CATALOG_HEADER} names ${shader_catalog_name}, but "
            "${shader_catalog_path} does not exist.")
    endif()
    list(APPEND SHADER_SOURCES "${shader_catalog_path}")
endforeach()
message(STATUS "Shader catalog: ${SHADER_CATALOG_SIZE} shaders")

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
        # --target-env must match the device contract in VulkanDeviceContext
        # (Vulkan 1.3). Declaring a newer environment lets glslc emit SPIR-V a
        # conforming 1.3 driver is allowed to reject.
        #
        # -O is deliberately not per-configuration. SHADER_OUTPUT_DIR has no
        # $<CONFIG> component, so Debug and Release share one .spv tree; a
        # config-dependent flag would bake in whichever config compiled first
        # and never rebuild. Optimize always, and pass -g by hand when a
        # specific shader needs to be stepped through.
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O -DMAX_SKIN_JOINTS=128 -fshader-stage=${shader_stage} "${shader_source}" -o "${shader_output}"
        DEPENDS "${shader_source}"
        VERBATIM
    )
endforeach()

add_custom_target(sokoban_shaders DEPENDS ${SHADER_OUTPUTS})

function(sokoban_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(SOKOBAN_ENABLE_SANITIZERS)
        if(MSVC)
            message(FATAL_ERROR
                "SOKOBAN_ENABLE_SANITIZERS currently requires a Clang or GCC toolchain")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        else()
            message(FATAL_ERROR
                "SOKOBAN_ENABLE_SANITIZERS requires a Clang or GCC toolchain")
        endif()
    endif()

    if(SOKOBAN_ENABLE_CLANG_TIDY)
        if(NOT SOKOBAN_CLANG_TIDY_EXECUTABLE)
            find_program(SOKOBAN_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
        endif()
        if(NOT SOKOBAN_CLANG_TIDY_EXECUTABLE)
            message(FATAL_ERROR
                "SOKOBAN_ENABLE_CLANG_TIDY is ON but clang-tidy was not found")
        endif()
        # Keep this a high-signal gate. Broader style check sets belong in a
        # separately agreed formatting policy rather than blocking every build.
        # Main files are always checked; restrict included-header diagnostics
        # to project-owned code so findings in third_party never gate CI.
        set(SOKOBAN_CLANG_TIDY_COMMAND
            "${SOKOBAN_CLANG_TIDY_EXECUTABLE}"
            "-checks=-*,clang-analyzer-*"
            "-warnings-as-errors=*"
            "--header-filter=^${CMAKE_CURRENT_SOURCE_DIR}/(src|tools)/"
        )
        set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY
            "${SOKOBAN_CLANG_TIDY_COMMAND}")
    endif()
endfunction()

if(SOKOBAN_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(
        RESULT SOKOBAN_LTO_SUPPORTED
        OUTPUT SOKOBAN_LTO_ERROR
        LANGUAGES CXX)
    if(NOT SOKOBAN_LTO_SUPPORTED)
        message(FATAL_ERROR
            "SOKOBAN_ENABLE_LTO was requested but is unavailable: ${SOKOBAN_LTO_ERROR}")
    endif()
endif()

function(sokoban_enable_lto target)
    if(SOKOBAN_ENABLE_LTO)
        set_property(TARGET ${target} PROPERTY
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

add_executable(sokoban_content_tool
    tools/ContentTool.cpp
)
target_include_directories(sokoban_content_tool PRIVATE src)
set_target_properties(sokoban_content_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools/$<CONFIG>"
)
sokoban_enable_warnings(sokoban_content_tool)
sokoban_enable_lto(sokoban_content_tool)

add_library(sokoban_core STATIC
    src/engine/ActionPlan.cpp
    src/engine/ActionScheduler.cpp
    src/engine/AnimationCatalog.cpp
    src/engine/AnimationCatalogEditor.cpp
    src/engine/AnimationEventSequencer.cpp
    src/engine/AnimationPreviewScene.cpp
    src/engine/AtomicFile.cpp
    src/engine/AsyncSaveStore.cpp
    src/engine/AssetManifest.cpp
    src/engine/AssetManifestEditor.cpp
    src/engine/AudioSystem.cpp
    src/engine/CampaignSession.cpp
    src/engine/BoardLayout.cpp
    src/engine/ContentPipeline.cpp
    src/engine/CrashDiagnostics.cpp
    src/engine/DecorationAssetRegistry.cpp
    src/engine/DecorationGizmo.cpp
    src/engine/DecorationMeshCatalog.cpp
    src/engine/EditorInteraction.cpp
    src/engine/FrameArena.cpp
    src/engine/GameplayPresentation.cpp
    src/engine/GameplayLoop.cpp
    src/engine/GameplaySession.cpp
    src/engine/Input.cpp
    src/engine/InputBindings.cpp
    src/engine/LevelTransition.cpp
    src/engine/Level.cpp
    src/engine/LevelCatalog.cpp
    src/engine/OverworldMap.cpp
    src/engine/OverworldMapEditor.cpp
    src/engine/OverworldView.cpp
    src/engine/LevelEditor.cpp
    src/engine/LevelProjectStore.cpp
    src/engine/Log.cpp
    src/engine/LogQueue.cpp
    src/engine/MirrorParticleEffect.cpp
    src/engine/MiniaudioImpl.cpp
    src/engine/ParticleSystem.cpp
    src/engine/PresentationSettings.cpp
    src/engine/PlayerProfile.cpp
    src/engine/PlayerProfileCodec.cpp
    src/engine/PlayerProfileMigrations.cpp
    src/engine/PresentationTransactionBuilder.cpp
    src/engine/RenderFrameBuilder.cpp
    src/engine/Reservation.cpp
    src/engine/Rules.cpp
    src/engine/RuntimeContent.cpp
    src/engine/SaveSlotManager.cpp
    src/engine/SaveStore.cpp
    src/engine/SettingsCoordinator.cpp
    src/engine/SettingsTypes.cpp
    src/engine/SplatCanvas.cpp
    src/engine/SplatPainter.cpp
    src/engine/StateDelta.cpp
    src/engine/TileThumbnailBake.cpp
    src/engine/TaskSystem.cpp
    src/engine/TextureSource.cpp
    src/engine/TileTypes.cpp
    src/engine/Window.cpp
    src/engine/render/AnimationController.cpp
    src/engine/render/AssetLoadScheduler.cpp
    src/engine/render/GltfMesh.cpp
    src/engine/render/FrameResourceTracker.cpp
    src/engine/render/FrameTimeTelemetry.cpp
    src/engine/render/GeometrySuballocator.cpp
    src/engine/render/GpuMaterial.cpp
    src/engine/render/GpuSkinning.cpp
    src/engine/render/ImageData.cpp
    src/engine/render/MaterialRenderPolicy.cpp
    src/engine/render/OpaqueDrawSorter.cpp
    src/engine/render/PbrMaterial.cpp
    src/engine/render/PngWriter.cpp
    src/engine/render/RenderAssetRequirements.cpp
    src/engine/render/RenderResolution.cpp
    src/engine/render/RuntimeTextureCatalog.cpp
    src/engine/render/TextureSourceLoader.cpp
    src/engine/render/Tonemap.cpp
    src/engine/render/UploadRingAllocator.cpp
    src/engine/render/RendererReconfiguration.cpp
    src/engine/render/IsoScenePreparer.cpp
)
add_library(Sokoban::core ALIAS sokoban_core)
target_include_directories(sokoban_core PUBLIC src)
target_include_directories(sokoban_core PRIVATE third_party/miniaudio)
target_link_libraries(sokoban_core
    PUBLIC SDL3::SDL3
    PRIVATE Threads::Threads sokoban_json sokoban_stb sokoban_cgltf
)
if(WIN32)
    target_link_libraries(sokoban_core PRIVATE dbghelp)
endif()
target_compile_definitions(sokoban_core PRIVATE
    SOKOBAN_GAME_VERSION="${PROJECT_VERSION}"
)
if(UNIX AND NOT APPLE)
    # miniaudio dynamically loads native audio backends on Unix-like systems.
    target_link_libraries(sokoban_core PRIVATE ${CMAKE_DL_LIBS} m)
endif()
sokoban_enable_warnings(sokoban_core)
sokoban_enable_lto(sokoban_core)

add_library(sokoban_ui STATIC
    src/engine/InputRouter.cpp
    src/engine/ShellFlow.cpp
    src/engine/ui/FontAtlas.cpp
    src/engine/ui/InputPrompts.cpp
    src/engine/ui/LevelCompleteOverlay.cpp
    src/engine/ui/MenuKit.cpp
    src/engine/ui/OptionsMenu.cpp
    src/engine/ui/SelectorPrompt.cpp
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
target_compile_definitions(sokoban_ui PUBLIC
    SOKOBAN_ENABLE_DEBUG_UI=$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_DEVELOPER_TOOLS}>>,1,0>
)
sokoban_enable_warnings(sokoban_ui)
sokoban_enable_lto(sokoban_ui)

add_library(sokoban_render_vulkan STATIC
    src/engine/render/VulkanRenderer.cpp
    src/engine/render/VulkanDeviceContext.cpp
    src/engine/render/VulkanDebugUtils.cpp
    src/engine/render/VulkanDiagnostics.cpp
    src/engine/render/VulkanDeviceSelection.cpp
    src/engine/render/VulkanGpuProfiler.cpp
    src/engine/render/VulkanGeometryArena.cpp
    src/engine/render/VulkanModelResources.cpp
    src/engine/render/VulkanUploadRing.cpp
    src/engine/render/VulkanPipelineCache.cpp
    src/engine/render/VulkanPipelineFactory.cpp
    src/engine/render/VulkanResourceUtils.cpp
    src/engine/render/VulkanSceneRecorder.cpp
    src/engine/render/VulkanSceneDescriptors.cpp
    src/engine/render/VulkanShadowPass.cpp
    src/engine/render/VulkanFrameCapture.cpp
    src/engine/render/VulkanThumbnailPass.cpp
    src/engine/render/VulkanSsaoPass.cpp
    src/engine/render/VulkanSwapchainResources.cpp
    src/engine/render/VulkanUiResources.cpp
)
add_library(Sokoban::render_vulkan ALIAS sokoban_render_vulkan)
if(SOKOBAN_ENABLE_DEVELOPER_TOOLS)
    foreach(imgui_source IN LISTS SOKOBAN_IMGUI_SOURCES)
        target_sources(sokoban_render_vulkan PRIVATE $<$<CONFIG:Debug>:${imgui_source}>)
    endforeach()
endif()
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
    SOKOBAN_ENABLE_DEBUG_UI=$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_DEVELOPER_TOOLS}>>,1,0>
)
target_compile_definitions(sokoban_render_vulkan PRIVATE
    SOKOBAN_ENABLE_VALIDATION=$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_VALIDATION}>>,1,0>
)
sokoban_enable_warnings(sokoban_render_vulkan)
sokoban_enable_lto(sokoban_render_vulkan)

target_link_libraries(sokoban_content_tool PRIVATE sokoban_core)

set(SOKOBAN_APPLICATION_SOURCES
    src/main.cpp
    src/engine/Application.cpp
)
if(SOKOBAN_ENABLE_DEVELOPER_TOOLS)
    list(APPEND SOKOBAN_APPLICATION_SOURCES
        $<$<CONFIG:Debug>:src/engine/AnimationCatalogDebugUi.cpp>
        $<$<CONFIG:Debug>:src/engine/AnimationPreviewDebugUi.cpp>
        $<$<CONFIG:Debug>:src/engine/ApplicationTools.cpp>
        $<$<CONFIG:Debug>:src/engine/ApplicationDebugUi.cpp>
        $<$<CONFIG:Debug>:src/engine/AssetManifestDebugUi.cpp>
        $<$<CONFIG:Debug>:src/engine/DebugUi.cpp>
        $<$<CONFIG:Debug>:src/engine/LevelEditorDebugUi.cpp>
    )
endif()
add_executable(sokoban ${SOKOBAN_APPLICATION_SOURCES})

if(WIN32)
    # Keep Explorer properties, the executable icon, and high-DPI behavior in
    # the binary instead of relying on developer-machine shortcuts.
    set(SOKOBAN_PRODUCT_NAME "Sokoban 3D")
    set(SOKOBAN_COPYRIGHT_YEAR "2026")
    set(SOKOBAN_WINDOWS_VERSION_COMMAS
        "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(SOKOBAN_ICON_RESOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Sokoban.ico")
    set(SOKOBAN_MANIFEST_RESOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Sokoban.manifest")
    if(NOT EXISTS "${SOKOBAN_ICON_RESOURCE}")
        message(FATAL_ERROR
            "Missing ${SOKOBAN_ICON_RESOURCE}. Run tools/GenerateWindowsIcon.ps1 first.")
    endif()
    file(TO_CMAKE_PATH "${SOKOBAN_ICON_RESOURCE}" SOKOBAN_ICON_RESOURCE)
    file(TO_CMAKE_PATH "${SOKOBAN_MANIFEST_RESOURCE}" SOKOBAN_MANIFEST_RESOURCE)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(SOKOBAN_WINDOWS_RESOURCE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/SokobanVersion.rc")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/SokobanVersion.rc.in"
        "${SOKOBAN_WINDOWS_RESOURCE_FILE}"
        @ONLY
    )
    target_sources(sokoban PRIVATE "${SOKOBAN_WINDOWS_RESOURCE_FILE}")
    # MSVC already owns manifest resource 1. Let LINK merge our application
    # manifest into that resource rather than defining a second one in .rc.
    target_link_options(sokoban PRIVATE
        "/MANIFESTINPUT:${SOKOBAN_MANIFEST_RESOURCE}")
endif()

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
    SOKOBAN_ENABLE_DEBUG_UI=$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_DEVELOPER_TOOLS}>>,1,0>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_DEVELOPER_TOOLS}>>:SOKOBAN_SOURCE_LEVEL_DIR="${LEVEL_SOURCE_DIR}">
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_DEVELOPER_TOOLS}>>:SOKOBAN_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets">
)

if(MSVC AND SOKOBAN_INSTALL_EXTERNAL_SYMBOLS)
    target_link_options(sokoban PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
endif()

sokoban_enable_warnings(sokoban)
sokoban_enable_lto(sokoban)

set(SOKOBAN_RUNTIME_SIGN_TARGET sokoban)
if(WIN32 AND SOKOBAN_SIGN_ARTIFACTS)
    if("${SOKOBAN_SIGNING_CERTIFICATE_THUMBPRINT}" STREQUAL "")
        message(FATAL_ERROR
            "SOKOBAN_SIGN_ARTIFACTS requires SOKOBAN_SIGNING_CERTIFICATE_THUMBPRINT. "
            "Set it from the signing environment; do not commit a certificate identity.")
    endif()
    find_program(SOKOBAN_SIGNTOOL_EXECUTABLE NAMES signtool.exe signtool
        HINTS "$ENV{WindowsSdkDir}/bin/x64")
    if(NOT SOKOBAN_SIGNTOOL_EXECUTABLE)
        file(GLOB SOKOBAN_SIGNTOOL_CANDIDATES
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/signtool.exe")
        list(SORT SOKOBAN_SIGNTOOL_CANDIDATES ORDER DESCENDING)
        if(SOKOBAN_SIGNTOOL_CANDIDATES)
            list(GET SOKOBAN_SIGNTOOL_CANDIDATES 0 SOKOBAN_SIGNTOOL_EXECUTABLE)
        endif()
    endif()
    if(NOT SOKOBAN_SIGNTOOL_EXECUTABLE OR NOT EXISTS "${SOKOBAN_SIGNTOOL_EXECUTABLE}")
        message(FATAL_ERROR
            "SOKOBAN_SIGN_ARTIFACTS requires signtool.exe from the Windows SDK")
    endif()
    add_custom_target(sokoban_sign_runtime
        COMMAND "${CMAKE_COMMAND}"
            "-DSIGNTOOL:FILEPATH=${SOKOBAN_SIGNTOOL_EXECUTABLE}"
            "-DARTIFACT:FILEPATH=$<TARGET_FILE:sokoban>"
            "-DTHUMBPRINT:STRING=${SOKOBAN_SIGNING_CERTIFICATE_THUMBPRINT}"
            "-DTIMESTAMP_URL:STRING=${SOKOBAN_SIGNING_TIMESTAMP_URL}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignWindowsArtifact.cmake"
        DEPENDS sokoban
        VERBATIM
    )
    set(SOKOBAN_RUNTIME_SIGN_TARGET sokoban_sign_runtime)
endif()

option(SOKOBAN_BUILD_TESTS "Build headless engine tests" ON)
option(SOKOBAN_BUILD_VULKAN_SMOKE_TESTS
    "Build the hidden-window Vulkan device and submission smoke test" ON)

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
    sokoban_enable_warnings(${ARG_TARGET})
    add_test(NAME ${ARG_NAME} COMMAND ${ARG_TARGET})
    if(ARG_ASSETS_ENV)
        set_tests_properties(${ARG_NAME} PROPERTIES
            ENVIRONMENT "SOKOBAN_ASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endif()
endfunction()

if(SOKOBAN_BUILD_TESTS)
    target_compile_definitions(sokoban_core PRIVATE SOKOBAN_ENABLE_TEST_HOOKS=1)
    enable_testing()

    if(SOKOBAN_BUILD_VULKAN_SMOKE_TESTS)
        sokoban_add_test(NAME vulkan_smoke TARGET sokoban_vulkan_smoke_tests
            LIBS sokoban_render_vulkan
            SOURCES tests/VulkanSmokeTests.cpp
        )
        set_tests_properties(vulkan_smoke PROPERTIES
            LABELS "vulkan;smoke"
            TIMEOUT 30
            SKIP_RETURN_CODE 77
        )
        if(SOKOBAN_ENABLE_SANITIZERS)
            set_property(TEST vulkan_smoke APPEND PROPERTY ENVIRONMENT
                "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/lsan.supp")
        endif()
    endif()

    # Header-only and dependency-free, so it links nothing: the point is that
    # the flags a CI gate is built on cannot silently mean nothing.
    sokoban_add_test(NAME command_line TARGET sokoban_command_line_tests
        SOURCES tests/CommandLineOptionsTests.cpp
    )
    # Also header-only. Math and Geometry underpin projection, skinning,
    # picking and (soon) culling, so their conventions are pinned here rather
    # than discovered from a wrong-looking frame.
    sokoban_add_test(NAME math TARGET sokoban_math_tests
        SOURCES tests/MathTests.cpp
    )
    sokoban_add_test(NAME geometry TARGET sokoban_geometry_tests
        SOURCES tests/GeometryTests.cpp
    )
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
    sokoban_add_test(NAME overworld_map TARGET sokoban_overworld_map_tests
        LIBS sokoban_core
        SOURCES tests/OverworldMapTests.cpp
    )
    sokoban_add_test(NAME gameplay_session TARGET sokoban_gameplay_session_tests
        LIBS sokoban_core
        SOURCES tests/GameplaySessionTests.cpp
    )
    sokoban_add_test(NAME player_profile TARGET sokoban_profile_tests
        LIBS sokoban_core sokoban_json
        SOURCES tests/PlayerProfileTests.cpp
    )
    target_compile_definitions(sokoban_profile_tests PRIVATE SOKOBAN_ENABLE_TEST_HOOKS=1)
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
    sokoban_add_test(NAME level_transition TARGET sokoban_level_transition_tests
        LIBS sokoban_core
        SOURCES tests/LevelTransitionTests.cpp
    )
    sokoban_add_test(NAME gameplay_loop TARGET sokoban_gameplay_loop_tests
        LIBS sokoban_core
        SOURCES tests/GameplayLoopTests.cpp
    )
    sokoban_add_test(NAME frame_pacing TARGET sokoban_frame_pacing_tests
        LIBS sokoban_core
        SOURCES tests/FramePacingTests.cpp
    )
    sokoban_add_test(NAME frame_descriptor_sync TARGET sokoban_frame_descriptor_sync_tests
        LIBS sokoban_render_vulkan
        SOURCES tests/FrameDescriptorSyncTests.cpp
    )
    sokoban_add_test(NAME frame_resource_tracker TARGET sokoban_frame_resource_tracker_tests
        LIBS sokoban_core
        SOURCES tests/FrameResourceTrackerTests.cpp
    )
    sokoban_add_test(NAME frame_time_telemetry TARGET sokoban_frame_time_telemetry_tests
        LIBS sokoban_core
        SOURCES tests/FrameTimeTelemetryTests.cpp
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
    sokoban_add_test(NAME decoration_mesh_catalog TARGET sokoban_decoration_mesh_catalog_tests
        LIBS sokoban_core
        SOURCES tests/DecorationMeshCatalogTests.cpp
    )
    sokoban_add_test(NAME decoration_gizmo TARGET sokoban_decoration_gizmo_tests
        LIBS sokoban_core
        SOURCES tests/DecorationGizmoTests.cpp
    )
    sokoban_add_test(NAME editor_interaction TARGET sokoban_editor_interaction_tests
        LIBS sokoban_core
        SOURCES tests/EditorInteractionTests.cpp
    )
    sokoban_add_test(NAME animation_controller TARGET sokoban_animation_controller_tests
        LIBS sokoban_core
        SOURCES tests/AnimationControllerTests.cpp
    )
    sokoban_add_test(NAME gltf_dependencies TARGET sokoban_gltf_dependency_tests
        LIBS sokoban_core
        SOURCES tests/GltfDependencyTests.cpp
    )
    sokoban_add_test(NAME runtime_texture_catalog TARGET sokoban_runtime_texture_catalog_tests
        LIBS sokoban_core
        ASSET_DIR
        SOURCES tests/RuntimeTextureCatalogTests.cpp
    )
    sokoban_add_test(NAME tonemap TARGET sokoban_tonemap_tests
        LIBS sokoban_core
        SOURCES tests/TonemapTests.cpp
    )
    sokoban_add_test(NAME pbr_material TARGET sokoban_pbr_material_tests
        LIBS sokoban_core
        SOURCES tests/PbrMaterialTests.cpp
    )
    sokoban_add_test(NAME animation_catalog TARGET sokoban_animation_catalog_tests
        LIBS sokoban_core
        ASSETS_ENV
        SOURCES tests/AnimationCatalogTests.cpp
    )
    sokoban_add_test(NAME animation_event_sequencer TARGET sokoban_animation_event_sequencer_tests
        LIBS sokoban_core
        ASSETS_ENV
        SOURCES tests/AnimationEventSequencerTests.cpp
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
    sokoban_add_test(NAME particles TARGET sokoban_particle_tests
        LIBS sokoban_core
        SOURCES tests/ParticleSystemTests.cpp
    )
    sokoban_add_test(NAME asset_requirements TARGET sokoban_asset_requirements_tests
        LIBS sokoban_core
        SOURCES tests/AssetRequirementsTests.cpp
    )
    sokoban_add_test(NAME asset_load_scheduler TARGET sokoban_asset_load_scheduler_tests
        LIBS sokoban_core
        SOURCES tests/AssetLoadSchedulerTests.cpp
    )
    sokoban_add_test(NAME geometry_suballocator TARGET sokoban_geometry_suballocator_tests
        LIBS sokoban_core
        SOURCES tests/GeometrySuballocatorTests.cpp
    )
    sokoban_add_test(NAME upload_ring_allocator TARGET sokoban_upload_ring_allocator_tests
        LIBS sokoban_core
        SOURCES tests/UploadRingAllocatorTests.cpp
    )
    sokoban_add_test(NAME gpu_skinning TARGET sokoban_gpu_skinning_tests
        LIBS sokoban_core
        ASSET_DIR
        SOURCES tests/GpuSkinningTests.cpp
    )
    sokoban_add_test(NAME gpu_material TARGET sokoban_gpu_material_tests
        LIBS sokoban_core
        SOURCES tests/GpuMaterialTests.cpp
    )
    sokoban_add_test(NAME opaque_draw_sorter TARGET sokoban_opaque_draw_sorter_tests
        LIBS sokoban_core
        SOURCES tests/OpaqueDrawSorterTests.cpp
    )
    sokoban_add_test(NAME tasks TARGET sokoban_task_tests
        LIBS sokoban_core
        SOURCES tests/TaskSystemTests.cpp
    )
    sokoban_add_test(NAME logging TARGET sokoban_log_tests
        LIBS sokoban_core
        SOURCES tests/LogTests.cpp
    )
    sokoban_add_test(NAME crash_diagnostics TARGET sokoban_crash_diagnostics_tests
        LIBS sokoban_core
        SOURCES tests/CrashDiagnosticsTests.cpp
    )
    sokoban_add_test(NAME frame_arena TARGET sokoban_frame_arena_tests
        LIBS sokoban_core
        SOURCES tests/FrameArenaTests.cpp
    )
    sokoban_add_test(NAME atomic_file TARGET sokoban_atomic_file_tests
        LIBS sokoban_core
        SOURCES tests/AtomicFileTests.cpp
    )
    target_compile_definitions(sokoban_atomic_file_tests PRIVATE SOKOBAN_ENABLE_TEST_HOOKS=1)
    sokoban_add_test(NAME level_project_store TARGET sokoban_level_project_store_tests
        LIBS sokoban_core
        SOURCES tests/LevelProjectStoreTests.cpp
    )
    target_compile_definitions(sokoban_level_project_store_tests PRIVATE
        SOKOBAN_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    sokoban_add_test(NAME overworld_map_editor TARGET sokoban_overworld_map_editor_tests
        LIBS sokoban_core sokoban_json
        SOURCES tests/OverworldMapEditorTests.cpp
    )
    sokoban_add_test(NAME content_pipeline TARGET sokoban_content_pipeline_tests
        LIBS sokoban_core
        SOURCES tests/ContentPipelineTests.cpp
    )
    sokoban_add_test(NAME splat_canvas TARGET sokoban_splat_canvas_tests
        LIBS sokoban_core
        SOURCES tests/SplatCanvasTests.cpp
    )
    sokoban_add_test(NAME splat_painter TARGET sokoban_splat_painter_tests
        LIBS sokoban_core
        SOURCES tests/SplatPainterTests.cpp
    )
    sokoban_add_test(NAME png_writer TARGET sokoban_png_writer_tests
        LIBS sokoban_core
        SOURCES tests/PngWriterTests.cpp
    )
    sokoban_add_test(NAME ground_pick TARGET sokoban_ground_pick_tests
        LIBS sokoban_core
        SOURCES tests/GroundPickTests.cpp
    )
    sokoban_add_test(NAME action_plan TARGET sokoban_action_plan_tests
        LIBS sokoban_core
        SOURCES tests/ActionPlanTests.cpp
    )
    sokoban_add_test(NAME action_scheduler TARGET sokoban_action_scheduler_tests
        LIBS sokoban_core
        SOURCES tests/ActionSchedulerTests.cpp
    )
    sokoban_add_test(NAME reservation TARGET sokoban_reservation_tests
        LIBS sokoban_core
        SOURCES tests/ReservationTests.cpp
    )
    sokoban_add_test(NAME state_delta TARGET sokoban_state_delta_tests
        LIBS sokoban_core
        SOURCES tests/StateDeltaTests.cpp
    )
    sokoban_add_test(NAME tile_thumbnail_bake TARGET sokoban_tile_thumbnail_bake_tests
        LIBS sokoban_core
        SOURCES tests/TileThumbnailBakeTests.cpp
    )
endif()

if(SOKOBAN_BUILD_FUZZ_TESTS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "SOKOBAN_BUILD_FUZZ_TESTS requires Clang's libFuzzer runtime")
    endif()

    # The harness receives the libFuzzer main at link time; core gets coverage
    # instrumentation but remains usable by ordinary executables.
    target_compile_options(sokoban_core PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
    )
    add_executable(sokoban_player_profile_fuzz
        tests/fuzz/PlayerProfileFuzz.cpp
    )
    target_include_directories(sokoban_player_profile_fuzz PRIVATE src)
    target_link_libraries(sokoban_player_profile_fuzz PRIVATE sokoban_core)
    target_compile_options(sokoban_player_profile_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(sokoban_player_profile_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
    sokoban_enable_warnings(sokoban_player_profile_fuzz)
endif()

install(TARGETS sokoban
    RUNTIME DESTINATION . COMPONENT Runtime
)
if(MSVC AND SOKOBAN_INSTALL_EXTERNAL_SYMBOLS)
    install(FILES "$<TARGET_PDB_FILE:sokoban>"
        DESTINATION symbols
        COMPONENT Symbols
    )
endif()
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
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf/LICENSE.txt"
    DESTINATION licenses
    RENAME cgltf-LICENSE.txt
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

if(WIN32 AND SOKOBAN_BUILD_INSTALLER)
    find_program(SOKOBAN_INNO_SETUP_COMPILER NAMES ISCC.exe ISCC
        HINTS
            "C:/Program Files (x86)/Inno Setup 6"
            "$ENV{ProgramW6432}/Inno Setup 6"
            "$ENV{ProgramFiles}/Inno Setup 6"
    )
    if(NOT SOKOBAN_INNO_SETUP_COMPILER)
        message(FATAL_ERROR
            "SOKOBAN_BUILD_INSTALLER requires Inno Setup 6 (ISCC.exe). "
            "Install it, or use the shipping preset for ZIP-only packaging.")
    endif()
    # Stage through the Runtime install component so the installer's file list
    # cannot accidentally include the optimized executable's external PDB.
    set(SOKOBAN_INSTALLER_STAGE_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/installer-stage")
    set(SOKOBAN_INSTALLER_OUTPUT_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/installer")
    file(TO_CMAKE_PATH "${SOKOBAN_INSTALLER_STAGE_DIR}" SOKOBAN_INSTALLER_STAGE_DIR)
    file(TO_CMAKE_PATH "${SOKOBAN_INSTALLER_OUTPUT_DIR}" SOKOBAN_INSTALLER_OUTPUT_DIR)
    set(SOKOBAN_INSTALLER_CONFIG
        "${CMAKE_CURRENT_BINARY_DIR}/generated/Sokoban3D.iss")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Sokoban3D.iss.in"
        "${SOKOBAN_INSTALLER_CONFIG}"
        @ONLY
    )
    set(SOKOBAN_INSTALLER_ARTIFACT
        "${SOKOBAN_INSTALLER_OUTPUT_DIR}/Sokoban3D-${PROJECT_VERSION}-Windows-x64-Setup.exe")
    add_custom_target(sokoban_installer
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
            --config Release --component Runtime
            --prefix "${SOKOBAN_INSTALLER_STAGE_DIR}"
        COMMAND "${SOKOBAN_INNO_SETUP_COMPILER}" "${SOKOBAN_INSTALLER_CONFIG}"
        DEPENDS ${SOKOBAN_RUNTIME_SIGN_TARGET}
        VERBATIM
    )
    if(SOKOBAN_SIGN_ARTIFACTS)
        add_custom_target(sokoban_sign_installer
            COMMAND "${CMAKE_COMMAND}"
                "-DSIGNTOOL:FILEPATH=${SOKOBAN_SIGNTOOL_EXECUTABLE}"
                "-DARTIFACT:FILEPATH=${SOKOBAN_INSTALLER_ARTIFACT}"
                "-DTHUMBPRINT:STRING=${SOKOBAN_SIGNING_CERTIFICATE_THUMBPRINT}"
                "-DTIMESTAMP_URL:STRING=${SOKOBAN_SIGNING_TIMESTAMP_URL}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignWindowsArtifact.cmake"
            DEPENDS sokoban_installer
            VERBATIM
        )
    endif()
endif()

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "Sokoban3D")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "${SOKOBAN_APP_PUBLISHER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Sokoban 3D puzzle game")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Sokoban 3D")
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
if(SOKOBAN_INSTALL_EXTERNAL_SYMBOLS)
    # The Runtime ZIP is player-facing. The matching PDB is emitted to a
    # distinct Symbols ZIP for crash reporting and is never bundled with it.
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL Runtime Symbols)
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Sokoban 3D Runtime")
    set(CPACK_COMPONENT_SYMBOLS_DISPLAY_NAME "Sokoban 3D Debug Symbols")
    set(CPACK_COMPONENT_SYMBOLS_DESCRIPTION
        "PDB files for matching crash dumps to the optimized shipping build")
endif()
include(CPack)

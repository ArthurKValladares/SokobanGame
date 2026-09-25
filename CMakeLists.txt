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
    # The Visual Studio generator hands every source in a project to one cl.exe
    # invocation, which compiles them one after another unless /MP is given;
    # MSBuild's own parallelism is only across projects. sokoban_core alone is
    # ninety sources. Ninja already runs one cl.exe per source, where /MP is
    # redundant, so it is added for the Visual Studio generators only.
    if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        add_compile_options(/MP)
    endif()
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
option(SOKOBAN_WARNINGS_AS_ERRORS
    "Fail the build on any compiler warning in Sokoban's own targets" OFF)
option(SOKOBAN_BUILD_FUZZ_TESTS
    "Build libFuzzer harnesses for hostile serialized input" OFF)
option(SOKOBAN_INCREMENTAL_CONTENT
    "Skip unchanged content stages and cache compressed textures in the build tree" ON)
option(SOKOBAN_BUILD_TESTS "Build engine tests" ON)
option(SOKOBAN_BUILD_VULKAN_SMOKE_TESTS
    "Build the hidden-window Vulkan device and submission smoke test" ON)
option(SOKOBAN_HEADLESS_TESTS_ONLY
    "Configure only Vulkan-independent libraries and tests" OFF)
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

if(SOKOBAN_HEADLESS_TESTS_ONLY)
    if(SOKOBAN_SHIPPING_BUILD OR SOKOBAN_BUILD_INSTALLER OR SOKOBAN_SIGN_ARTIFACTS)
        message(FATAL_ERROR
            "SOKOBAN_HEADLESS_TESTS_ONLY cannot be combined with shipping, installer, or signing options")
    endif()
    # A headless configuration is a test-only build contract. Keep these
    # values deterministic even when a reused cache previously enabled the
    # application or Vulkan smoke coverage.
    set(SOKOBAN_BUILD_TESTS ON CACHE BOOL "Build engine tests" FORCE)
    set(SOKOBAN_BUILD_VULKAN_SMOKE_TESTS OFF CACHE BOOL
        "Build the hidden-window Vulkan device and submission smoke test" FORCE)
    set(SOKOBAN_ENABLE_DEVELOPER_TOOLS OFF CACHE BOOL
        "Compile the Debug-only ImGui workspace and content-editing tools" FORCE)
endif()

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
    set(SOKOBAN_INCREMENTAL_CONTENT OFF CACHE BOOL
        "Skip unchanged content stages and cache compressed textures in the build tree" FORCE)
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

if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
    find_package(Vulkan REQUIRED)
endif()
find_package(Threads REQUIRED)

add_library(sokoban_json INTERFACE)
target_include_directories(sokoban_json SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

add_library(sokoban_stb INTERFACE)
target_include_directories(sokoban_stb SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)

# Optimized in every configuration; see cmake/bc7enc16/CMakeLists.txt.
add_subdirectory(cmake/bc7enc16)

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
if(NOT SOKOBAN_HEADLESS_TESTS_ONLY
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp")
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

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_INCLUDE_DIR "${SHADER_SOURCE_DIR}/include")
set(LEVEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/levels")

if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

# Declarations shared by more than one shader - the GPU-side mirrors of
# GpuDrawInstance, SceneFrameUniform, GpuMaterial and GpuSkinningInstance -
# live here and are pulled in with #include rather than retyped per module.
#
# Globbed rather than listed because these are a build dependency, not a
# compilation unit: a header nobody lists is a shader that silently does not
# rebuild when the ABI under it changes, which is the same class of failure
# ShaderCatalog.hpp exists to prevent. CONFIGURE_DEPENDS re-globs so a new
# header is picked up without a manual reconfigure.
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS "${SHADER_INCLUDE_DIR}/*.glsl")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")

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
list(LENGTH SHADER_CATALOG_NAMES SHADER_CATALOG_NAME_COUNT)
# The catalog's array deduces its own size, so there is no declared count to
# cross-check against - that check, and the regex that read the count out of
# the C++, are both gone. What is worth keeping is the guard against the
# match itself failing: zero names means the header was reformatted past what
# this regex understands, and the build would otherwise cheerfully compile no
# shaders at all and fail at launch.
if(SHADER_CATALOG_NAME_COUNT LESS 1)
    message(FATAL_ERROR
        "Found no shader filenames in ${SHADER_CATALOG_HEADER}. Every entry "
        "must be a plain quoted string literal ending in .vert.glsl or "
        ".frag.glsl; a name assembled from pieces is invisible here.")
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
message(STATUS "Shader catalog: ${SHADER_CATALOG_NAME_COUNT} shaders")

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
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O -DMAX_SKIN_JOINTS=128 -fshader-stage=${shader_stage} -I "${SHADER_INCLUDE_DIR}" "${shader_source}" -o "${shader_output}"
        # Every shared header, not just this module's: glslc resolves
        # includes itself, so CMake has no view of which shader reads
        # which header. Depending on all of them rebuilds 16 small
        # modules when one changes, which costs about a second and
        # cannot leave a stale .spv behind.
        DEPENDS "${shader_source}" ${SHADER_INCLUDES}
        VERBATIM
    )
endforeach()

add_custom_target(sokoban_shaders DEPENDS ${SHADER_OUTPUTS})
endif()

# Warnings, and optionally the gate that makes them mean something.
#
# The flags below were on from the start; nothing enforced them, so a warning
# introduced today would have sat unnoticed among the rest. Measured before
# switching the gate on: compiling every file under src/ with GCC 11 and
# -Wall -Wextra -Wpedantic produced 942 warnings, 940 of them the one
# diagnostic suppressed below; the other two were fixed. So the gate costs
# nothing to keep green. It is on in CI and off locally, because a warning
# should stop a merge, not stop someone mid-edit.
#
# That census is a floor, not a guarantee: CI builds on ubuntu-24.04 with the
# distribution's default GCC and its `clang` package, both newer than the GCC
# that produced the count, and newer compilers know diagnostics older ones do
# not. The first CI run with this on is the real measurement.
function(sokoban_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(SOKOBAN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        # GCC reports a missing initializer for every field a *designated*
        # initializer does not name. That is the initialization style this
        # codebase uses for Vulkan structs everywhere - `.sType = ...,
        # .flags = ...` with the rest deliberately value-initialized - so the
        # diagnostic fires about a thousand times and is right about none of
        # them. Clang and MSVC do not warn here. Suppressed rather than worked
        # around, because the alternative is naming every field of every
        # VkCreateInfo by hand.
        target_compile_options(${target} PRIVATE
            -Wno-missing-field-initializers)
        if(SOKOBAN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
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
        # The check set, the reason for it and the measurement behind it live
        # in .clang-tidy at the repository root, not here - a list of checks
        # wants the room to say why each one is on the list. This deliberately
        # passes no -checks argument, because one would override that file.
        #
        # --header-filter stays here because it needs the absolute source
        # directory, which the config file cannot know; it keeps diagnostics
        # from third_party headers out of the gate.
        set(SOKOBAN_CLANG_TIDY_COMMAND
            "${SOKOBAN_CLANG_TIDY_EXECUTABLE}"
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

if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
    add_executable(sokoban_content_tool
        tools/ContentTool.cpp
    )
    target_include_directories(sokoban_content_tool PRIVATE src)
    set_target_properties(sokoban_content_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools/$<CONFIG>"
    )
    sokoban_enable_warnings(sokoban_content_tool)
    sokoban_enable_lto(sokoban_content_tool)
endif()

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
    src/engine/LevelAssetAssociations.cpp
    src/engine/Log.cpp
    src/engine/LogQueue.cpp
    src/engine/MirrorParticleEffect.cpp
    src/engine/MiniaudioImpl.cpp
    src/engine/ParticleSystem.cpp
    src/engine/TurretParticleEffect.cpp
    src/engine/PresentationSettings.cpp
    src/engine/PlayerProfile.cpp
    src/engine/PlayerProfileCodec.cpp
    src/engine/PlayerProfileMigrations.cpp
    src/engine/PresentationTransactionBuilder.cpp
    src/engine/RenderFrameBuilder.cpp
    src/engine/RenderFrameBuilderEditor.cpp
    src/engine/RenderFrameParts.cpp
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
    src/engine/render/AtmosphereMath.cpp
    src/engine/render/AssetLoadScheduler.cpp
    src/engine/render/CompressedTextureArtifact.cpp
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
    src/engine/render/PointShadowFaceCache.cpp
    src/engine/render/RenderAssetRequirements.cpp
    src/engine/render/RenderResolution.cpp
    src/engine/render/RuntimeTextureCatalog.cpp
    src/engine/render/SsaoMath.cpp
    src/engine/render/TextureSourceLoader.cpp
    src/engine/render/TextureMipResidency.cpp
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
        sokoban_bc7enc16
)
if(WIN32)
    target_link_libraries(sokoban_core PRIVATE dbghelp)
endif()
target_compile_definitions(sokoban_core PRIVATE
    SOKOBAN_GAME_VERSION="${PROJECT_VERSION}"
)

# Defined once, PUBLIC, on the library every other target links. This flag
# decides whether Application and DebugUi declare some of their members, so a
# translation unit that saw a different value would disagree with the rest of
# the program about the size and layout of those classes - an ODR violation
# that links cleanly and misbehaves at runtime. The headers used to guard
# against an undefined flag by quietly defining it to 0 themselves, which is
# what made that failure silent; they now refuse to compile instead, and this
# is the definition that keeps them satisfied.
target_compile_definitions(sokoban_core PUBLIC
    SOKOBAN_ENABLE_DEBUG_UI=$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_DEVELOPER_TOOLS}>>,1,0>
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
sokoban_enable_warnings(sokoban_ui)
sokoban_enable_lto(sokoban_ui)

if(MSVC)
    set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS /W0)
else()
    set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS -w)
endif()

if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
add_library(sokoban_render_vulkan STATIC
    src/engine/render/VulkanRenderer.cpp
    src/engine/render/VulkanDeviceContext.cpp
    src/engine/render/VulkanDebugUtils.cpp
    src/engine/render/VulkanDiagnostics.cpp
    src/engine/render/VulkanDeviceSelection.cpp
    src/engine/render/VulkanGpuProfiler.cpp
    src/engine/render/VulkanGeometryArena.cpp
    src/engine/render/VulkanMemoryAllocator.cpp
    src/engine/render/VulkanMemoryAllocatorImplementation.cpp
    src/engine/render/VulkanModelResources.cpp
    src/engine/render/VulkanTextureUploader.cpp
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
        third_party/vma
)
target_link_libraries(sokoban_render_vulkan
    PUBLIC
        sokoban_core
        sokoban_ui
        Vulkan::Vulkan
        SDL3::SDL3
)
target_compile_definitions(sokoban_render_vulkan PRIVATE
    SOKOBAN_ENABLE_VALIDATION=$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${SOKOBAN_ENABLE_VALIDATION}>>,1,0>
)
sokoban_enable_warnings(sokoban_render_vulkan)
sokoban_enable_lto(sokoban_render_vulkan)

# VMA is a vendored single-header implementation. Keep third-party diagnostics
# out of the project's warning budget while retaining warnings for the wrapper.
if(MSVC)
    set_source_files_properties(
        src/engine/render/VulkanMemoryAllocatorImplementation.cpp
        PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(
        src/engine/render/VulkanMemoryAllocatorImplementation.cpp
        PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_link_libraries(sokoban_content_tool PRIVATE sokoban_core)

set(SOKOBAN_APPLICATION_SOURCES
    src/main.cpp
    src/engine/Application.cpp
    src/engine/ApplicationEvidence.cpp
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
        $<$<CONFIG:Debug>:src/engine/LogDebugUi.cpp>
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
#
# The target has no declared outputs, so it runs on every build; which files a
# stage reads is only known once the manifest and glTF documents are parsed.
# Development builds therefore pass --incremental: the tool still collects and
# validates the inventory (about 0.1 s), then leaves the tree alone when its
# stage record says nothing changed. Compressed textures come from a
# build-local cache keyed by source bytes, so a stage that does run re-encodes
# only textures whose inputs changed. Both used to be absent, and BC7 encoding
# every texture on every build cost about 29 s of each Debug build. Shipping
# builds keep the clean, uncached stage their package gate was validated with.
set(SOKOBAN_CONTENT_STAGE_OPTIONS)
if(SOKOBAN_INCREMENTAL_CONTENT)
    list(APPEND SOKOBAN_CONTENT_STAGE_OPTIONS
        --incremental
        # Per configuration: floating-point code generation differs between
        # optimization levels, so Debug and Release tools can encode the same
        # image to slightly different (equally valid) blocks. Keeping their
        # caches apart keeps each configuration's package its own.
        --texture-cache "${CMAKE_CURRENT_BINARY_DIR}/content-cache/$<CONFIG>"
    )
endif()
add_custom_target(sokoban_content ALL
    COMMAND "$<TARGET_FILE:sokoban_content_tool>"
        --assets "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        --levels "${LEVEL_SOURCE_DIR}"
        --shaders "${SHADER_OUTPUT_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets"
        --version "${PROJECT_VERSION}"
        ${SOKOBAN_CONTENT_STAGE_OPTIONS}
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
target_link_libraries(sokoban PRIVATE
    sokoban_core
    sokoban_ui
    sokoban_render_vulkan
)

target_compile_definitions(sokoban PRIVATE
    SDL_MAIN_USE_CALLBACKS=0
    SOKOBAN_GAME_VERSION="${PROJECT_VERSION}"
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
endif()

# Declares one headless test suite and registers it with CTest.
#   sokoban_add_test(
#       NAME <ctest name>
#       SOURCES <files...>
#       [LIBS <libraries...>]
#       [DEFINITIONS <compile definitions...>]  # for this suite's sources only
#       [DEPENDS <targets...>]                  # built before the suite
#       [ASSET_DIR]   # compiles with SOKOBAN_TEST_ASSET_DIR = source assets
#       [ASSETS_ENV]  # runs with SOKOBAN_ASSETS env var = source assets
#       [STANDALONE TARGET <executable name>]
#   )
#
# Suites share runner executables rather than each being its own program:
# sokoban_vulkan_tests for suites that link sokoban_render_vulkan, and
# sokoban_tests for the rest. With one executable per suite, every change to
# sokoban_core relinked 76 test programs (3.7 GB of Debug PDBs on Windows);
# now it relinks the runners. CTest still sees one test per suite, with the
# same name, so -R, labels, timeouts, and per-suite environments are
# unchanged: each test runs `<runner> <name> [args...]` in its own process.
#
# Each suite keeps its own `int main()` and still reads as a normal program.
# Its sources are compiled with main defined to sokobanTestSuite_<name>, and
# the runner (tests/TestRunner.cpp) dispatches to that function by name. The
# runner's suite table is generated from these declarations, including
# whether each main takes (argc, argv).
#
# STANDALONE keeps a suite in its own executable. Use it only when the suite
# changes something program-wide, such as replacing global operator new, that
# must not leak into other suites.
#
# SOKOBAN_TEST_RUNNERS=OFF gives every suite its own executable again, named
# sokoban_<name>_tests. It defaults to OFF in sanitizer builds: GNU ld needs
# far more memory for an ASan/UBSan runner than for the game (2.7-3.7 GB for
# each third of sokoban_tests, and more than the 6 GB available for all of
# it, against 0.7 GB for sokoban) because instrumented globals multiply the
# sections it has to order. Separate links keep that job within an ordinary
# CI runner, and sanitizer builds are not an inner loop.
if(SOKOBAN_ENABLE_SANITIZERS)
    set(SOKOBAN_TEST_RUNNERS_DEFAULT OFF)
else()
    set(SOKOBAN_TEST_RUNNERS_DEFAULT ON)
endif()
option(SOKOBAN_TEST_RUNNERS
    "Link test suites into shared runner executables instead of one each"
    ${SOKOBAN_TEST_RUNNERS_DEFAULT})

function(sokoban_add_test)
    cmake_parse_arguments(ARG "ASSET_DIR;ASSETS_ENV;STANDALONE" "NAME;TARGET"
        "SOURCES;LIBS;DEFINITIONS;DEPENDS" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sokoban_add_test requires NAME and SOURCES")
    endif()
    if(NOT ARG_NAME MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR
            "Test name ${ARG_NAME} must be a lowercase identifier; it also "
            "names the suite's entry point")
    endif()
    if(ARG_STANDALONE AND NOT ARG_TARGET)
        message(FATAL_ERROR "STANDALONE test ${ARG_NAME} requires TARGET")
    elseif(ARG_TARGET AND NOT ARG_STANDALONE)
        message(FATAL_ERROR
            "Test ${ARG_NAME} names TARGET without STANDALONE; runner suites "
            "have no executable of their own")
    endif()
    foreach(test_source IN LISTS ARG_SOURCES)
        if(test_source MATCHES "(^|[/\\\\])src[/\\\\]")
            message(FATAL_ERROR
                "Test ${ARG_NAME} compiles production source ${test_source}; "
                "link its owning Sokoban library instead")
        endif()
    endforeach()

    set(definitions ${ARG_DEFINITIONS})
    if(ARG_ASSET_DIR)
        list(APPEND definitions
            SOKOBAN_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endif()

    if(ARG_STANDALONE OR NOT SOKOBAN_TEST_RUNNERS)
        if(ARG_TARGET)
            set(target ${ARG_TARGET})
        else()
            set(target sokoban_${ARG_NAME}_tests)
        endif()
        add_executable(${target} ${ARG_SOURCES})
        target_include_directories(${target} PRIVATE src)
        target_compile_definitions(${target} PRIVATE ${definitions})
        sokoban_enable_warnings(${target})
        add_test(NAME ${ARG_NAME} COMMAND ${target})
    else()
        if(sokoban_render_vulkan IN_LIST ARG_LIBS)
            set(target sokoban_vulkan_tests)
        else()
            set(target sokoban_tests)
        endif()
        if(NOT TARGET ${target})
            add_executable(${target} tests/TestRunner.cpp)
            target_include_directories(${target} PRIVATE
                src
                "${CMAKE_CURRENT_BINARY_DIR}/generated/tests/${target}")
            target_link_libraries(${target} PRIVATE sokoban_ui)
            sokoban_enable_warnings(${target})
            set_property(GLOBAL APPEND PROPERTY SOKOBAN_TEST_RUNNERS ${target})
        endif()
        target_sources(${target} PRIVATE ${ARG_SOURCES})
        set_property(SOURCE ${ARG_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS
            main=sokobanTestSuite_${ARG_NAME} ${definitions})

        # The runner's table needs to know how to call this suite's main. This
        # is read at configure time: changing a suite between main() and
        # main(argc, argv) needs a reconfigure, or the runner fails to link
        # with an undefined sokobanTestSuite_<name>.
        set(arity "")
        foreach(test_source IN LISTS ARG_SOURCES)
            file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}" text)
            if(text MATCHES "\nint main\\(\\)")
                set(arity NO_ARGS)
            elseif(text MATCHES "\nint main\\(int [A-Za-z_]+, char\\*\\* [A-Za-z_]+\\)")
                set(arity WITH_ARGS)
            endif()
        endforeach()
        if(NOT arity)
            message(FATAL_ERROR
                "Test ${ARG_NAME}: none of its sources defines `int main()` or "
                "`int main(int argc, char** argv)` at the start of a line")
        endif()
        set_property(GLOBAL APPEND PROPERTY SOKOBAN_TEST_SUITES_${target}
            "SOKOBAN_TEST_SUITE_${arity}(${ARG_NAME})")
        add_test(NAME ${ARG_NAME} COMMAND ${target} ${ARG_NAME})
    endif()

    if(ARG_LIBS)
        target_link_libraries(${target} PRIVATE ${ARG_LIBS})
    endif()
    if(ARG_DEPENDS)
        add_dependencies(${target} ${ARG_DEPENDS})
    endif()
    if(ARG_ASSETS_ENV)
        set_tests_properties(${ARG_NAME} PROPERTIES
            ENVIRONMENT "SOKOBAN_ASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endif()
endfunction()

# Writes each runner's suite table once every suite has been declared.
function(sokoban_finalize_test_runners)
    get_property(runners GLOBAL PROPERTY SOKOBAN_TEST_RUNNERS)
    foreach(runner IN LISTS runners)
        get_property(suites GLOBAL PROPERTY SOKOBAN_TEST_SUITES_${runner})
        list(JOIN suites "\n" table)
        file(CONFIGURE
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/tests/${runner}/TestSuites.inc"
            CONTENT "// Generated by sokoban_add_test in CMakeLists.txt.\n${table}\n"
            @ONLY)
    endforeach()
endfunction()

if(SOKOBAN_BUILD_TESTS)
    target_compile_definitions(sokoban_core PRIVATE SOKOBAN_ENABLE_TEST_HOOKS=1)
    enable_testing()

    if(SOKOBAN_BUILD_VULKAN_SMOKE_TESTS)
        target_compile_definitions(sokoban_render_vulkan PRIVATE
            SOKOBAN_ENABLE_TEST_HOOKS=1)
        sokoban_add_test(NAME vulkan_smoke
            LIBS sokoban_render_vulkan
            SOURCES tests/VulkanSmokeTests.cpp
            DEFINITIONS
                SOKOBAN_ENABLE_TEST_HOOKS=1
                SOKOBAN_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
                SOKOBAN_GAME_VERSION="${PROJECT_VERSION}"
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

        add_test(NAME application_validation_teardown
            COMMAND "${CMAKE_COMMAND}"
                "-DSOKOBAN_EXECUTABLE=$<TARGET_FILE:sokoban>"
                "-DSOKOBAN_SAVE_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/test-output/application-validation-teardown/$<CONFIG>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyValidationTeardownExit.cmake"
        )
        set_tests_properties(application_validation_teardown PROPERTIES
            LABELS "vulkan;smoke"
            TIMEOUT 40
        )
    endif()

    # Header-only and dependency-free: the point is that the flags a CI gate
    # is built on cannot silently mean nothing. It links nothing of its own;
    # the shared runner brings the libraries, which it does not use.
    sokoban_add_test(NAME command_line
        SOURCES tests/CommandLineOptionsTests.cpp
    )
    # Also header-only. Math and Geometry underpin projection, skinning,
    # picking and (soon) culling, so their conventions are pinned here rather
    # than discovered from a wrong-looking frame.
    sokoban_add_test(NAME math
        SOURCES tests/MathTests.cpp
    )
    sokoban_add_test(NAME geometry
        SOURCES tests/GeometryTests.cpp
    )
    sokoban_add_test(NAME rules
        LIBS sokoban_core
        SOURCES tests/RulesTests.cpp
    )
    sokoban_add_test(NAME level
        LIBS sokoban_core
        SOURCES tests/LevelTests.cpp
    )
    sokoban_add_test(NAME level_catalog
        LIBS sokoban_core
        SOURCES tests/LevelCatalogTests.cpp
    )
    sokoban_add_test(NAME overworld_map
        LIBS sokoban_core
        SOURCES tests/OverworldMapTests.cpp
    )
    sokoban_add_test(NAME gameplay_session
        LIBS sokoban_core
        SOURCES tests/GameplaySessionTests.cpp
    )
    # Standalone because it replaces global operator new and delete to measure
    # allocations, which must not apply to the suites sharing a runner.
    sokoban_add_test(NAME player_profile STANDALONE TARGET sokoban_profile_tests
        LIBS sokoban_core sokoban_json
        SOURCES tests/PlayerProfileTests.cpp
        DEFINITIONS SOKOBAN_ENABLE_TEST_HOOKS=1
    )
    sokoban_add_test(NAME save_slots
        LIBS sokoban_core sokoban_json
        SOURCES tests/SaveSlotManagerTests.cpp
    )
    sokoban_add_test(NAME shell_flow
        LIBS sokoban_ui
        SOURCES tests/ShellFlowTests.cpp
    )
    sokoban_add_test(NAME input
        LIBS sokoban_core
        SOURCES tests/InputTests.cpp
    )
    sokoban_add_test(NAME image_data
        LIBS sokoban_core
        ASSET_DIR
        SOURCES tests/ImageDataTests.cpp
    )
    sokoban_add_test(NAME ui
        LIBS sokoban_ui
        ASSET_DIR
        SOURCES tests/UiTests.cpp
    )
    sokoban_add_test(NAME title
        LIBS sokoban_ui
        ASSET_DIR
        SOURCES tests/TitleScreenTests.cpp
    )
    if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
        sokoban_add_test(NAME vulkan_device_selection
            LIBS sokoban_render_vulkan
            SOURCES tests/VulkanDeviceSelectionTests.cpp
        )
        # Needs no device: vkCheck only formats and throws.
        sokoban_add_test(NAME vulkan_diagnostics
            LIBS sokoban_render_vulkan
            SOURCES tests/VulkanDiagnosticsTests.cpp
        )
    endif()
    sokoban_add_test(NAME input_router
        LIBS sokoban_ui
        SOURCES tests/InputRouterTests.cpp
    )
    sokoban_add_test(NAME settings_coordinator
        LIBS sokoban_core
        SOURCES tests/SettingsCoordinatorTests.cpp
    )
    sokoban_add_test(NAME campaign_session
        LIBS sokoban_core
        SOURCES tests/CampaignSessionTests.cpp
    )
    sokoban_add_test(NAME level_transition
        LIBS sokoban_core
        SOURCES tests/LevelTransitionTests.cpp
    )
    sokoban_add_test(NAME gameplay_loop
        LIBS sokoban_core
        SOURCES tests/GameplayLoopTests.cpp
    )
    sokoban_add_test(NAME frame_pacing
        LIBS sokoban_core
        SOURCES tests/FramePacingTests.cpp
    )
    if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
        sokoban_add_test(NAME frame_descriptor_sync
            LIBS sokoban_render_vulkan
            SOURCES tests/FrameDescriptorSyncTests.cpp
        )
    endif()
    sokoban_add_test(NAME frame_resource_tracker
        LIBS sokoban_core
        SOURCES tests/FrameResourceTrackerTests.cpp
    )
    sokoban_add_test(NAME frame_time_telemetry
        LIBS sokoban_core
        SOURCES tests/FrameTimeTelemetryTests.cpp
    )
    sokoban_add_test(NAME renderer_reconfiguration
        LIBS sokoban_core
        SOURCES tests/RendererReconfigurationTests.cpp
    )
    sokoban_add_test(NAME iso_scene_preparer
        LIBS sokoban_core
        SOURCES tests/IsoScenePreparerTests.cpp
    )
    sokoban_add_test(NAME level_editor
        LIBS sokoban_core
        SOURCES tests/LevelEditorTests.cpp
    )
    sokoban_add_test(NAME decoration_mesh_catalog
        LIBS sokoban_core
        SOURCES tests/DecorationMeshCatalogTests.cpp
    )
    sokoban_add_test(NAME decoration_gizmo
        LIBS sokoban_core
        SOURCES tests/DecorationGizmoTests.cpp
    )
    sokoban_add_test(NAME editor_interaction
        LIBS sokoban_core
        SOURCES tests/EditorInteractionTests.cpp
    )
    sokoban_add_test(NAME animation_controller
        LIBS sokoban_core
        SOURCES tests/AnimationControllerTests.cpp
    )
    sokoban_add_test(NAME gltf_dependencies
        LIBS sokoban_core
        SOURCES tests/GltfDependencyTests.cpp
    )
    sokoban_add_test(NAME runtime_texture_catalog
        LIBS sokoban_core
        ASSET_DIR
        SOURCES tests/RuntimeTextureCatalogTests.cpp
    )
    sokoban_add_test(NAME compressed_texture_artifact
        LIBS sokoban_core
        SOURCES tests/CompressedTextureArtifactTests.cpp
    )
    sokoban_add_test(NAME tonemap
        LIBS sokoban_core
        SOURCES tests/TonemapTests.cpp
    )
    sokoban_add_test(NAME pbr_material
        LIBS sokoban_core
        SOURCES tests/PbrMaterialTests.cpp
    )
    sokoban_add_test(NAME ssao_math
        LIBS sokoban_core
        SOURCES tests/SsaoMathTests.cpp
    )
    sokoban_add_test(NAME atmosphere_math
        LIBS sokoban_core
        SOURCES tests/AtmosphereMathTests.cpp
    )
    sokoban_add_test(NAME animation_catalog
        LIBS sokoban_core
        ASSETS_ENV
        SOURCES tests/AnimationCatalogTests.cpp
    )
    sokoban_add_test(NAME animation_event_sequencer
        LIBS sokoban_core
        ASSETS_ENV
        SOURCES tests/AnimationEventSequencerTests.cpp
    )
    sokoban_add_test(NAME asset_manifest
        LIBS sokoban_core sokoban_json
        ASSETS_ENV
        SOURCES tests/AssetManifestTests.cpp
    )
    sokoban_add_test(NAME asset_manifest_editor
        LIBS sokoban_core
        ASSETS_ENV
        SOURCES tests/AssetManifestEditorTests.cpp
    )
    sokoban_add_test(NAME presentation
        LIBS sokoban_core
        SOURCES tests/PresentationTests.cpp
    )
    sokoban_add_test(NAME particles
        LIBS sokoban_core
        SOURCES tests/ParticleSystemTests.cpp
    )
    sokoban_add_test(NAME asset_requirements
        LIBS sokoban_core
        SOURCES tests/AssetRequirementsTests.cpp
    )
    sokoban_add_test(NAME asset_load_state
        LIBS sokoban_core
        SOURCES tests/AssetLoadStateTests.cpp
    )
    sokoban_add_test(NAME residency_budget
        LIBS sokoban_core
        SOURCES tests/ResidencyBudgetTests.cpp
    )
    sokoban_add_test(NAME asset_load_scheduler
        LIBS sokoban_core
        SOURCES tests/AssetLoadSchedulerTests.cpp
    )
    sokoban_add_test(NAME geometry_suballocator
        LIBS sokoban_core
        SOURCES tests/GeometrySuballocatorTests.cpp
    )
    sokoban_add_test(NAME upload_ring_allocator
        LIBS sokoban_core
        SOURCES tests/UploadRingAllocatorTests.cpp
    )
    sokoban_add_test(NAME material_range_allocator
        LIBS sokoban_core
        SOURCES tests/MaterialRangeAllocatorTests.cpp
    )
    sokoban_add_test(NAME gpu_skinning
        LIBS sokoban_core
        ASSET_DIR
        SOURCES tests/GpuSkinningTests.cpp
    )
    sokoban_add_test(NAME gpu_material
        LIBS sokoban_core
        SOURCES tests/GpuMaterialTests.cpp
    )
    # Reads the layout out of the .spv modules the build just produced and
    # compares it to offsetof() on the matching struct, so the GLSL and C++
    # descriptions of each GPU block cannot drift apart unnoticed. Needs the
    # shaders built, and needs to be told where they landed - SHADER_OUTPUT_DIR
    # has no $<CONFIG> component, so one path serves every configuration.
    if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
        sokoban_add_test(NAME gpu_abi
            LIBS sokoban_core
            SOURCES tests/GpuAbiTests.cpp
            DEFINITIONS SOKOBAN_TEST_SHADER_DIR="${SHADER_OUTPUT_DIR}"
            DEPENDS sokoban_shaders
        )
    endif()
    # Reads DrawMode.glsl as text and compares it to DrawMaterialMode. Needs
    # the shader *source* directory, not the compiled one: the value it checks
    # is folded into a branch by the optimizer and cannot be read back out of
    # SPIR-V, so the header itself is the only place to see it.
    sokoban_add_test(NAME draw_mode
        LIBS sokoban_core
        SOURCES tests/DrawModeTests.cpp
        DEFINITIONS SOKOBAN_TEST_SHADER_INCLUDE_DIR="${SHADER_INCLUDE_DIR}"
    )
    sokoban_add_test(NAME scene_draw_lanes
        LIBS sokoban_core
        SOURCES tests/SceneDrawLanesTests.cpp
    )
    sokoban_add_test(NAME texture_descriptor_space
        LIBS sokoban_core
        SOURCES tests/TextureDescriptorSpaceTests.cpp
    )
    if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
        # Header-only and constexpr; it needs the Vulkan enums, not a device.
        sokoban_add_test(NAME texture_upload_plan
            LIBS sokoban_render_vulkan
            SOURCES tests/TextureUploadPlanTests.cpp
        )
    endif()
    sokoban_add_test(NAME opaque_draw_sorter
        LIBS sokoban_core
        SOURCES tests/OpaqueDrawSorterTests.cpp
    )
    sokoban_add_test(NAME tasks
        LIBS sokoban_core
        SOURCES tests/TaskSystemTests.cpp
        DEFINITIONS SOKOBAN_ENABLE_TEST_HOOKS=1
    )
    sokoban_add_test(NAME logging
        LIBS sokoban_core
        SOURCES tests/LogTests.cpp
    )
    sokoban_add_test(NAME crash_diagnostics
        LIBS sokoban_core
        SOURCES tests/CrashDiagnosticsTests.cpp
    )
    sokoban_add_test(NAME frame_arena
        LIBS sokoban_core
        SOURCES tests/FrameArenaTests.cpp
    )
    sokoban_add_test(NAME atomic_file
        LIBS sokoban_core
        SOURCES tests/AtomicFileTests.cpp
        DEFINITIONS SOKOBAN_ENABLE_TEST_HOOKS=1
    )
    sokoban_add_test(NAME level_project_store
        LIBS sokoban_core
        SOURCES tests/LevelProjectStoreTests.cpp
        DEFINITIONS SOKOBAN_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    sokoban_add_test(NAME overworld_map_editor
        LIBS sokoban_core sokoban_json
        SOURCES tests/OverworldMapEditorTests.cpp
    )
    sokoban_add_test(NAME content_pipeline
        LIBS sokoban_core
        SOURCES tests/ContentPipelineTests.cpp
    )
    sokoban_add_test(NAME splat_canvas
        LIBS sokoban_core
        SOURCES tests/SplatCanvasTests.cpp
    )
    sokoban_add_test(NAME splat_painter
        LIBS sokoban_core
        SOURCES tests/SplatPainterTests.cpp
    )
    sokoban_add_test(NAME png_writer
        LIBS sokoban_core
        SOURCES tests/PngWriterTests.cpp
    )
    sokoban_add_test(NAME ground_pick
        LIBS sokoban_core
        SOURCES tests/GroundPickTests.cpp
    )
    sokoban_add_test(NAME action_plan
        LIBS sokoban_core
        SOURCES tests/ActionPlanTests.cpp
    )
    sokoban_add_test(NAME action_scheduler
        LIBS sokoban_core
        SOURCES tests/ActionSchedulerTests.cpp
    )
    sokoban_add_test(NAME reservation
        LIBS sokoban_core
        SOURCES tests/ReservationTests.cpp
    )
    sokoban_add_test(NAME state_delta
        LIBS sokoban_core
        SOURCES tests/StateDeltaTests.cpp
    )
    sokoban_add_test(NAME tile_thumbnail_bake
        LIBS sokoban_core
        SOURCES tests/TileThumbnailBakeTests.cpp
    )
    if(WIN32 AND NOT SOKOBAN_HEADLESS_TESTS_ONLY)
        find_program(SOKOBAN_POWERSHELL_EXECUTABLE NAMES powershell REQUIRED)
        add_executable(sokoban_package_validation_fixture
            tests/PackageValidationFixture.cpp
        )
        sokoban_enable_warnings(sokoban_package_validation_fixture)
        add_dependencies(sokoban_package_validation_fixture sokoban)
        add_test(NAME shipping_package_validation
            COMMAND "${SOKOBAN_POWERSHELL_EXECUTABLE}"
                -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyShippingPackageGate.ps1"
                -Validator "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ValidateShippingPackage.ps1"
                -FixtureExecutable "$<TARGET_FILE:sokoban_package_validation_fixture>"
                -ApplicationExecutable "$<TARGET_FILE:sokoban>"
                -WorkingRoot "${CMAKE_CURRENT_BINARY_DIR}/test-output/shipping-package-validation/$<CONFIG>"
                -GameVersion "${PROJECT_VERSION}"
        )
        set_tests_properties(shipping_package_validation PROPERTIES
            LABELS "packaging"
            TIMEOUT 45
        )
    endif()

    sokoban_finalize_test_runners()
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

if(NOT SOKOBAN_HEADLESS_TESTS_ONLY)
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
endif()

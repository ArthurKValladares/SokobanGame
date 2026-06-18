cmake_minimum_required(VERSION 3.25)

project(Sokoban3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SOKOBAN_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

find_package(Vulkan REQUIRED)

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

find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders")

set(SHADER_SOURCES
    "${SHADER_SOURCE_DIR}/triangle.vert.glsl"
    "${SHADER_SOURCE_DIR}/triangle.frag.glsl"
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
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.4 -fshader-stage=${shader_stage} "${shader_source}" -o "${shader_output}"
        DEPENDS "${shader_source}"
        VERBATIM
    )
endforeach()

add_custom_target(sokoban_shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(sokoban
    src/main.cpp
    src/engine/Application.cpp
    src/engine/Input.cpp
    src/engine/Window.cpp
    src/engine/render/VulkanRenderer.cpp
)

add_dependencies(sokoban sokoban_shaders)

target_include_directories(sokoban PRIVATE src)
target_link_libraries(sokoban PRIVATE Vulkan::Vulkan SDL3::SDL3)

target_compile_definitions(sokoban PRIVATE
    SDL_MAIN_USE_CALLBACKS=0
    SOKOBAN_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}/assets"
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

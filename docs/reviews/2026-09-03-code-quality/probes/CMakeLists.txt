cmake_minimum_required(VERSION 3.25)
project(SokobanReview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
if(NOT MSVC)
  message(FATAL_ERROR "This evidence harness links the existing Windows/MSVC project build.")
endif()
add_executable(review_probes probes.cpp)
target_include_directories(review_probes PRIVATE "${REPO}/src" "${REPO}/third_party" "${REPO}/third_party/SDL/include")
target_compile_definitions(review_probes PRIVATE SOKOBAN_ENABLE_TEST_HOOKS=1 SOKOBAN_ENABLE_DEBUG_UI=$<IF:$<CONFIG:Debug>,1,0>)
target_link_libraries(review_probes PRIVATE
  "${REPO}/build/$<CONFIG>/sokoban_ui.lib"
  "${REPO}/build/$<CONFIG>/sokoban_core.lib"
  "${REPO}/build/third_party/SDL/$<CONFIG>/SDL3-static.lib"
  "${REPO}/build/$<CONFIG>/sokoban_bc7enc16.lib"
  kernel32 user32 gdi32 winmm imm32 ole32 oleaut32 version uuid advapi32 setupapi shell32 dinput8 dbghelp)

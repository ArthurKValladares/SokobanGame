# The vendored BC7 encoder, always compiled with optimization.
#
# The content tool BC7-compresses every texture it stages, and in a Debug build
# an unoptimized encoder dominates the whole build: 24.8 s of a 25.2 s no-op
# build in the 2026-09-24 developer-iteration review, 11.2 s once only this
# library was optimized, and 6.9 s in Release. Nothing here is worth stepping
# through, so every configuration uses the Release flags.
#
# That cannot be done with a target option alone. MSVC's Debug flags carry
# /RTC1, which cl rejects alongside /O2, and CMake before 3.30 has no per-target
# runtime-check property. CMAKE_<LANG>_FLAGS_<CONFIG> are read per directory,
# so this library lives in its own directory and replaces them here. The
# runtime library (CMAKE_MSVC_RUNTIME_LIBRARY), debug-information format, and
# the root's add_compile_options still apply, so the objects remain
# link-compatible with the rest of each configuration.
foreach(sokoban_bc7_config IN ITEMS DEBUG RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_CXX_FLAGS_${sokoban_bc7_config} "${CMAKE_CXX_FLAGS_RELEASE}")
endforeach()

set(SOKOBAN_BC7ENC16_SOURCE
    "${PROJECT_SOURCE_DIR}/third_party/bc7enc16/bc7enc16.c")
add_library(sokoban_bc7enc16 STATIC "${SOKOBAN_BC7ENC16_SOURCE}")
set_source_files_properties(
    "${SOKOBAN_BC7ENC16_SOURCE}"
    PROPERTIES LANGUAGE CXX SKIP_LINTING ON)
target_include_directories(sokoban_bc7enc16 SYSTEM PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/bc7enc16"
)
if(MSVC)
    target_compile_options(sokoban_bc7enc16 PRIVATE /w)
else()
    target_compile_options(sokoban_bc7enc16 PRIVATE -w)
endif()

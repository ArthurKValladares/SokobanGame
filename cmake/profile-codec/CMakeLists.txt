# Debug builds can spend seconds encoding and validating a long undo history.
# Isolate the codec so MSVC can optimize it without the Debug /RTC flags.
# Keep the Debug runtime, symbols and preprocessor definitions.
string(REGEX REPLACE "(^|[ \t])[-/](Ob0|Od|RTC[1csu]+)" ""
    CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

add_library(sokoban_profile_codec OBJECT
    "${PROJECT_SOURCE_DIR}/src/engine/PlayerProfileCodec.cpp")
target_include_directories(sokoban_profile_codec PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sokoban_profile_codec PRIVATE SDL3::SDL3 sokoban_json)
target_compile_definitions(sokoban_profile_codec PRIVATE
    "SOKOBAN_ENABLE_DEBUG_UI=$<IF:${SOKOBAN_DEVELOPER_TOOLS_ENABLED},1,0>")
target_compile_options(sokoban_profile_codec PRIVATE "$<$<CONFIG:Debug>:/O2>")
sokoban_enable_warnings(sokoban_profile_codec)
sokoban_enable_lto(sokoban_profile_codec)

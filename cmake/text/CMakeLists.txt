# Pinned, local source archives keep every preset usable without network access.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(FT_DISABLE_ZLIB ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)
set(FT_DISABLE_HVF ON)
set(SKIP_INSTALL_ALL ON)
set(HB_BUILD_UTILS OFF)
set(HB_BUILD_SUBSET OFF)
set(HB_BUILD_RASTER OFF)
set(HB_BUILD_VECTOR OFF)
set(HB_BUILD_GPU ON)
set(HB_BUILD_GPU_DEMO OFF)
set(HB_HAVE_CORETEXT OFF)
set(HB_HAVE_ICU OFF)
set(HB_HAVE_GLIB OFF)
set(SB_CONFIG_UNITY ON)
set(SB_CONFIG_EXPERIMENTAL_TEXT_API OFF)
set(BUILD_GENERATOR OFF)
set(text_archives "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/text")
FetchContent_Declare(sokoban_freetype
    URL "${text_archives}/freetype-2.14.3.zip"
    URL_HASH SHA256=3f624983444f9e9149707853a792dc5a897f3afc9f23a9b8669c03ac564779d1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sokoban_harfbuzz
    URL "${text_archives}/harfbuzz-14.4.0.zip"
    URL_HASH SHA256=9e387a5aab810bb1da7f76a2b6e834667ec56bf9eaf8f7c5d97cfabfd6d12788
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sokoban_sheenbidi
    URL "${text_archives}/sheenbidi-3.0.0.zip"
    URL_HASH SHA256=bb524e3b04a6c8ae166027e7e71b5807e4f0ad7c5a8353aac625ba4dc06f8c46
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sokoban_unibreak
    URL "${text_archives}/libunibreak-8.0.zip"
    URL_HASH SHA256=a50cb9d2f557386470ee5cb92c75507afbb52778e237cfae129397e2d491e9f4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR sokoban-no-upstream-cmake)
FetchContent_MakeAvailable(sokoban_freetype sokoban_harfbuzz sokoban_sheenbidi sokoban_unibreak)
add_library(sokoban_unibreak STATIC
    "${sokoban_unibreak_SOURCE_DIR}/src/unibreakbase.c"
    "${sokoban_unibreak_SOURCE_DIR}/src/unibreakdef.c"
    "${sokoban_unibreak_SOURCE_DIR}/src/linebreak.c"
    "${sokoban_unibreak_SOURCE_DIR}/src/linebreakdata.c"
    "${sokoban_unibreak_SOURCE_DIR}/src/linebreakdef.c"
    "${sokoban_unibreak_SOURCE_DIR}/src/eastasianwidthdef.c"
    "${sokoban_unibreak_SOURCE_DIR}/src/emojidef.c"
    "${sokoban_unibreak_SOURCE_DIR}/src/graphemebreak.c")
target_include_directories(sokoban_unibreak SYSTEM PUBLIC "${sokoban_unibreak_SOURCE_DIR}/src")
add_library(sokoban_text_dependencies INTERFACE)
target_link_libraries(sokoban_text_dependencies INTERFACE freetype harfbuzz harfbuzz-gpu SheenBidi sokoban_unibreak)

cmake_minimum_required(VERSION 3.10)
project(MacMDWrite C)

# Optimize for size: on a 68000 smaller code is usually faster too,
# and section GC strips unused library code.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -ffunction-sections -fdata-sections -Wall -Wno-multichar")

set(SOURCES
    src/main.c
    src/editor.c
    src/layout.c
    src/markdown.c
    src/encoding.c
    src/document.c
    src/chrome.c
    src/journal.c
    src/files.c
    src/library.c
    src/textbuf.c
    src/MacMDWrite.r
    src/fonts.r
)

add_application(MacMDWrite ${SOURCES} TYPE "APPL" CREATOR "MMDW")
set_target_properties(MacMDWrite PROPERTIES LINK_FLAGS "-Wl,-gc-sections")

# Font comparison: the same app with grid-fitted MDW Mono strikes
# (tools/make_fonts.py --fitted). Built when FONT_TEST is on (CI does).
option(FONT_TEST "Build the grid-fitted font comparison disk" OFF)
if(FONT_TEST)
    set(FITTED_SOURCES ${SOURCES})
    list(REMOVE_ITEM FITTED_SOURCES src/fonts.r)
    list(APPEND FITTED_SOURCES src/fonts_fitted.r)
    add_application(MacMDWrite_FittedFont ${FITTED_SOURCES} TYPE "APPL" CREATOR "MMDW")
    target_compile_definitions(MacMDWrite_FittedFont PRIVATE FONT_FITTED)
    set_target_properties(MacMDWrite_FittedFont PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
endif()

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
    src/textbuf.c
    src/MacMDWrite.r
    src/fonts.r
)

add_application(MacMDWrite ${SOURCES} TYPE "APPL" CREATOR "MMDW")
set_target_properties(MacMDWrite PROPERTIES LINK_FLAGS "-Wl,-gc-sections")

# Diagnostic variants, each with one subsystem switched off. Build with
# -DDIAGNOSTICS=ON (CI does) to narrow down a crash in one test pass.
option(DIAGNOSTICS "Build diagnostic variants" OFF)
if(DIAGNOSTICS)
    foreach(V NO_MENUHIDE NO_CHROME NO_OFFSCREEN)
        add_application(Diag_${V} ${SOURCES} TYPE "APPL" CREATOR "MMDW")
        target_compile_definitions(Diag_${V} PRIVATE DIAG_${V})
        set_target_properties(Diag_${V} PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
    endforeach()
endif()

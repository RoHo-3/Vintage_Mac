cmake_minimum_required(VERSION 3.10)
project(MacMDWrite C)

# Optimize for size: on a 68000 smaller code is usually faster too,
# and section GC strips unused library code.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -ffunction-sections -fdata-sections -Wall")

add_application(MacMDWrite
    src/main.c
    src/MacMDWrite.r
    TYPE "APPL"
    CREATOR "MMDW"
)

set_target_properties(MacMDWrite PROPERTIES
    LINK_FLAGS "-Wl,-gc-sections"
)

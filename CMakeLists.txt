cmake_minimum_required(VERSION 2.8...3.13)
project(vd-link)

add_subdirectory(${CMAKE_SOURCE_DIR}/librtp)

# Set target platform: "gs" (default) or "drone"
set(PLATFORM "gs" CACHE STRING "Target platform: gs or drone")

message(STATUS "Selected platform: ${PLATFORM}")

# Validate the PLATFORM value
if(NOT PLATFORM STREQUAL "gs" AND NOT PLATFORM STREQUAL "drone")
    message(FATAL_ERROR "Unknown platform: ${PLATFORM}. Must be 'gs' or 'drone'")
endif()

# Include platform-specific CMake configuration
add_subdirectory(${PLATFORM})

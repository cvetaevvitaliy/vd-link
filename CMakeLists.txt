cmake_minimum_required(VERSION 2.8...3.13)

if(UNIX AND NOT APPLE)
    # Linux
    set(OS_SUPPORTED TRUE)
elseif(APPLE)
    # macOS
    set(OS_SUPPORTED TRUE)
else()
    set(OS_SUPPORTED FALSE)
endif()

if(OS_SUPPORTED)
    execute_process(
            COMMAND git rev-parse --abbrev-ref HEAD
            COMMAND cut -c1-20
            OUTPUT_VARIABLE GIT_BRANCH
            RESULT_VARIABLE BRANCH_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    if(BRANCH_RESULT EQUAL 0)
        message(STATUS "Git branch: ${GIT_BRANCH}")
        add_definitions(-DGIT_BRANCH=\"${GIT_BRANCH}\")
    else()
        message(STATUS "Git branch not found")
        add_definitions(-DGIT_BRANCH=\"\")
    endif()

    execute_process(
            COMMAND git rev-parse --short=8 HEAD
            OUTPUT_VARIABLE GIT_HASH
            RESULT_VARIABLE HASH_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    if(HASH_RESULT EQUAL 0)
        message(STATUS "Git hash: ${GIT_HASH}")
        add_definitions(-DGIT_HASH=\"${GIT_HASH}\")
    else()
        message(STATUS "Git hash not found")
        add_definitions(-DGIT_HASH=\"-.-.-\")
    endif()

    execute_process(
            COMMAND git describe --tags --abbrev=0
            OUTPUT_VARIABLE GIT_TAG
            RESULT_VARIABLE TAG_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    if(TAG_RESULT EQUAL 0)
        message(STATUS "Git tag: ${GIT_TAG}")
        add_definitions(-DGIT_TAG=\"${GIT_TAG}\")
    else()
        message(STATUS "Git tag not found")
        add_definitions(-DGIT_TAG=\"unknown\")
    endif()
endif()

project(vd-link)

add_subdirectory(${CMAKE_SOURCE_DIR}/librtp)

# Set target platform: "gs" (default) or "drone"
set(PLATFORM "gs" CACHE STRING "Target platform: gs or drone")

message(STATUS "Selected platform: ${PLATFORM}")
add_definitions(-DPLATFORM=\"${PLATFORM}\")

# Validate the PLATFORM value
if(NOT PLATFORM STREQUAL "gs" AND NOT PLATFORM STREQUAL "drone")
    message(FATAL_ERROR "Unknown platform: ${PLATFORM}. Must be 'gs' or 'drone'")
endif()

# Include platform-specific CMake configuration
add_subdirectory(${PLATFORM})

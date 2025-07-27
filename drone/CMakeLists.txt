cmake_minimum_required(VERSION 2.8...3.13)
project(vd-link C)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_C_FLAGS_DEBUG "-O1 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

include_directories(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/librtp/include
)


add_executable(${PROJECT_NAME}
        src/main.c
)

target_link_libraries(${PROJECT_NAME} rtp)


###########################################################################
# install step

# Install to /usr/local/bin/
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

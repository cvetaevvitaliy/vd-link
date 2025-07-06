# Full source code https://github.com/ireader/media-server.git
# This file is part of the iReader Media Server project.
# just added RTP and wrote cmake
cmake_minimum_required(VERSION 2.8...3.13)
project(rtp C)

set(CMAKE_C_STANDARD 99)

file(GLOB SOURCES source/*.c rtpext/*.c payload/*.c)
add_definitions(-DOS_LINUX)

add_library(${PROJECT_NAME} STATIC ${SOURCES})
target_include_directories (${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include )

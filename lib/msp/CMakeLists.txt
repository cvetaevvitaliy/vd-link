cmake_minimum_required(VERSION 2.8...3.13)
project(msp C)

set(CMAKE_C_STANDARD 99)

file(GLOB SOURCES msp.c )

add_library(${PROJECT_NAME} STATIC ${SOURCES})
target_include_directories (${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
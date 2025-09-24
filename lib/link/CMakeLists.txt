project(link)

set(CMAKE_C_STANDARD 99)

add_library(link STATIC
    link.c
    link.h
)

target_include_directories(link PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include_common
)
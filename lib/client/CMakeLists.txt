cmake_minimum_required(VERSION 2.8...3.13)

add_library(drone_client STATIC
    drone_client.c
)

target_include_directories(drone_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(drone_client 
    pthread
)

target_compile_options(drone_client PRIVATE
    -Wall
    -Wextra
    -std=c99
)

install(TARGETS drone_client
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES drone_client.h
    DESTINATION include
)
cmake_minimum_required(VERSION 2.8...3.13)
project(pidfile C)

set(CMAKE_C_STANDARD 99)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/pidfile.c
        )

add_library(${PROJECT_NAME} STATIC ${SRC})

target_include_directories(${PROJECT_NAME} 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
)
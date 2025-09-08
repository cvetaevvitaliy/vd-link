cmake_minimum_required(VERSION 3.13)
project(bytetrack_wrapper C CXX)

set(CMAKE_CXX_STANDARD 14)
option(REBUILD_BYTETRACK "Rebuild ByteTrack-cpp library instead of using precompiled .so file" OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../detection)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wno-psabi")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wno-psabi")

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/bytetrack_api.cpp
        )

add_library(${PROJECT_NAME} STATIC ${SRC})
target_include_directories(${PROJECT_NAME} PUBLIC ${eigen_SOURCE_DIR})
set_target_properties(${PROJECT_NAME} PROPERTIES
    PUBLIC_HEADER "bytetrack_api.h"
)

IF(REBUILD_BYTETRACK)
    include(FetchContent)
    
    FetchContent_Declare(
        bytetrack
        GIT_REPOSITORY https://github.com/Vertical-Beach/ByteTrack-cpp.git
        GIT_TAG        d43805d461a714f65da039981bd5f5d21cf5cf59
    )
    FetchContent_MakeAvailable(bytetrack)
    FetchContent_MakeAvailable(eigen)
    target_link_libraries(${PROJECT_NAME} bytetrack)
    target_include_directories(${PROJECT_NAME} PUBLIC ${bytetrack_SOURCE_DIR}/include)
    target_include_directories(${PROJECT_NAME} PUBLIC ${eigen_SOURCE_DIR})
    install(PROGRAMS ${bytetrack_BIN_DIR}/libbytetrack.so DESTINATION lib)
ELSE()
    set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/libbytetrack.so)
    install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/libbytetrack.so DESTINATION lib)
ENDIF()
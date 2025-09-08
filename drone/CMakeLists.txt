cmake_minimum_required(VERSION 2.8...3.13)
project(vd-link-drone C)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_C_FLAGS_DEBUG "-O1 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/detection)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bytetrack)

find_package(PkgConfig REQUIRED)

include_directories(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_isp
        ${CMAKE_CURRENT_SOURCE_DIR}/detection
        ${CMAKE_CURRENT_SOURCE_DIR}/bytetrack
        ${CMAKE_SOURCE_DIR}/librtp/include
        ${CMAKE_SOURCE_DIR}/pidfile
        ${CMAKE_SOURCE_DIR}/link
)

pkg_check_modules(RkMedia REQUIRED IMPORTED_TARGET
        libeasymedia
)

pkg_check_modules(CAIRO REQUIRED cairo)
include_directories(${CAIRO_INCLUDE_DIRS})
link_directories(${CAIRO_LIBRARY_DIRS})

find_package(PkgConfig QUIET)
pkg_check_modules(RKAIQ QUIET "rkaiq")

include(FindPackageHandleStandardArgs)
find_path(RKAIQ_INCLUDE_DIR
        NAMES
        common/rk_aiq.h
        PATH
        include
        PATH_SUFFIXES
        rkaiq)
find_library(RKAIQ_LIBRARY
        rkaiq)
find_package_handle_standard_args(RKAIQ
        DEFAULT_MSG
        RKAIQ_INCLUDE_DIR
        RKAIQ_LIBRARY
)
mark_as_advanced(
        RKAIQ_INCLUDE_DIR
        RKAIQ_LIBRARY)

if(RKAIQ_FOUND)
    set(RKAIQ_LIBRARIES    ${RKAIQ_LIBRARY})
    set(RKAIQ_INCLUDE_DIRS
            ${RKAIQ_INCLUDE_DIR}/algos;
            ${RKAIQ_INCLUDE_DIR}/algos/a3dlut;
            ${RKAIQ_INCLUDE_DIR}/algos/ablc;
            ${RKAIQ_INCLUDE_DIR}/algos/accm;
            ${RKAIQ_INCLUDE_DIR}/algos/acp;
            ${RKAIQ_INCLUDE_DIR}/algos/adebayer;
            ${RKAIQ_INCLUDE_DIR}/algos/adehaze;
            ${RKAIQ_INCLUDE_DIR}/algos/adpcc;
            ${RKAIQ_INCLUDE_DIR}/algos/ae;
            ${RKAIQ_INCLUDE_DIR}/algos/af;
            ${RKAIQ_INCLUDE_DIR}/algos/agamma;
            ${RKAIQ_INCLUDE_DIR}/algos/ahdr;
            ${RKAIQ_INCLUDE_DIR}/algos/aie;
            ${RKAIQ_INCLUDE_DIR}/algos/alsc;
            ${RKAIQ_INCLUDE_DIR}/algos/anr;
            ${RKAIQ_INCLUDE_DIR}/algos/aorb;
            ${RKAIQ_INCLUDE_DIR}/algos/asd;
            ${RKAIQ_INCLUDE_DIR}/algos/asharp;
            ${RKAIQ_INCLUDE_DIR}/algos/awb;
            ${RKAIQ_INCLUDE_DIR}/common;
            ${RKAIQ_INCLUDE_DIR}/iq_parser;
            ${RKAIQ_INCLUDE_DIR}/uAPI;
            ${RKAIQ_INCLUDE_DIR}/xcore)

    if(RKAIQ_LIBRARY AND NOT TARGET RkAiq::RkAiq)
        if(IS_ABSOLUTE "${RKAIQ_LIBRARY}")
            add_library(RkAiq::RkAiq UNKNOWN IMPORTED)
            set_target_properties(RkAiq::RkAiq PROPERTIES IMPORTED_LOCATION
                    "${RKAIQ_LIBRARY}")
        else()
            add_library(RkAiq::RkAiq INTERFACE IMPORTED)
            set_target_properties(RkAiq::RkAiq PROPERTIES IMPORTED_LIBNAME
                    "${RKAIQ_LIBRARY}")
        endif()
        set_target_properties(RkAiq::RkAiq PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                "${RKAIQ_INCLUDE_DIR}")
    endif()
endif()
include_directories(${RKAIQ_INCLUDE_DIRS})


add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_isp/sample_common_isp.c
)

target_link_libraries(${PROJECT_NAME} rga easymedia ${RKAIQ_LIBRARY} pidfile pthread m rtp ${CAIRO_LIBRARIES} detection bytetrack_wrapper link)


###########################################################################
# install step

# Install to /usr/local/bin/
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

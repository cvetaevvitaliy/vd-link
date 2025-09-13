cmake_minimum_required(VERSION 2.8...3.13)
project(vd-link-${PLATFORM} C)

if (CMAKE_SYSROOT) # already defined
    message(STATUS "CMAKE_SYSROOT already defined: ${CMAKE_SYSROOT}")
else()
    if(DEFINED ENV{SYSROOT})
        set(CMAKE_SYSROOT $ENV{SYSROOT})
        message(STATUS "CMAKE_SYSROOT from ENV: ${CMAKE_SYSROOT}")
    else()
        message(STATUS "CMAKE_SYSROOT not defined and ENV{SYSROOT} not defined")
        set(CMAKE_SYSROOT "/opt/sdk/arm-buildroot-linux-gnueabihf/sysroot")
    endif()
    message(STATUS "SYSROOT: ${CMAKE_SYSROOT}")
    message(STATUS "CMAKE_FIND_ROOT_PATH: ${CMAKE_FIND_ROOT_PATH}")
    set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endif()

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
pkg_check_modules(CAIRO REQUIRED cairo)
include_directories(${CAIRO_INCLUDE_DIRS})
link_directories(${CAIRO_LIBRARY_DIRS})
find_package(PkgConfig REQUIRED)
pkg_check_modules(RkMedia REQUIRED libeasymedia)

# Find RkAiq headers and library
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

include_directories(${PROJECT_NAME}
        ${RKAIQ_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/librtp/include
)


add_executable(${PROJECT_NAME}
        src/main.c
        src/encoder.c
)

target_link_libraries(${PROJECT_NAME}
        rga
        easymedia
        pthread
        m
        rtp
        ${CAIRO_LIBRARIES}
        ${RKAIQ_LIBRARY}
)


###########################################################################
# install step

# Install to /usr/local/bin/
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

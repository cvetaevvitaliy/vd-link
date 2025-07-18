cmake_minimum_required(VERSION 3.13)
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

# Include FetchContent for external dependencies
include(FetchContent)

# LVGL configuration
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_interface/lv_conf.h CACHE STRING "LVGL configuration file path")

# Fetch LVGL from GitHub
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v8.3.11
    GIT_SHALLOW TRUE
)

# Configure LVGL as a static library
set(LVGL_BUILD_SHARED OFF CACHE BOOL "Build LVGL as a static library" FORCE)

# Make LVGL available
FetchContent_MakeAvailable(lvgl)

add_subdirectory(librtp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
pkg_check_modules(LIBRGA REQUIRED IMPORTED_TARGET librga)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(ROCKCHIPMPP REQUIRED IMPORTED_TARGET rockchip_mpp)
pkg_check_modules(MSGPACK REQUIRED IMPORTED_TARGET msgpack)

message(STATUS "ZLIB_INCLUDE_DIRS: ${ZLIB_INCLUDE_DIRS}")
message(STATUS "ZLIB_LIBRARIES: ${ZLIB_LIBRARIES}")
message(STATUS "LIBRGA_INCLUDE_DIRS: ${LIBRGA_INCLUDE_DIRS}")
message(STATUS "LIBRGA_LIBRARIES: ${LIBRGA_LIBRARIES}")
message(STATUS "LIBDRM_INCLUDE_DIRS: ${LIBDRM_INCLUDE_DIRS}")
message(STATUS "LIBDRM_LIBRARIES: ${LIBDRM_LIBRARIES}")
message(STATUS "ROCKCHIPMPP_INCLUDE_DIRS: ${ROCKCHIPMPP_INCLUDE_DIRS}")
message(STATUS "ROCKCHIPMPP_LIBRARIES: ${ROCKCHIPMPP_LIBRARIES}")
message(STATUS "ROCKCHIPMPP_VERSION: ${ROCKCHIPMPP_VERSION}")
message(STATUS "MSGPACK_INCLUDE_DIRS: ${MSGPACK_INCLUDE_DIRS}")

include_directories(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
        ${MPP_INCLUDE_DIRS}
        ${LIBDRM_INCLUDE_DIRS}
        ${LIBRGA_INCLUDE_DIRS}
        ${ROCKCHIPMPP_INCLUDE_DIRS}
        ${MSGPACK_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/librtp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/msp-osd
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_interface  # For lv_conf.h
)

set(MSP_OSD_SRC
        src/msp-osd/fakehd/fakehd.c
        src/msp-osd/font/font.c
        src/msp-osd/json/osd_config.c
        src/msp-osd/json/parson.c
        src/msp-osd/libspng/spng.c
        src/msp-osd/lz4/lz4.c
        src/msp-osd/msp/msp.c
        src/msp-osd/msp/msp_displayport.c
#        src/msp-osd/net/network.c
#        src/msp-osd/net/serial.c
#        src/msp-osd/rec/rec.c
#        src/msp-osd/rec/rec_pb.c
#        src/msp-osd/rec/rec_shim.c
#        src/msp-osd/rec/rec_util.c
        src/msp-osd/toast/toast.c
        src/msp-osd/util/fs_util.c
)

add_executable(${PROJECT_NAME}
        ${MSP_OSD_SRC}
        src/main.c
        src/drm_display.c
        src/rtp_receiver.c
        src/decoder.c
        src/msp-osd.c
        src/wfb_status_link.c
        src/ui_interface/interface_lvgl.c
        src/ui_interface/compositor.c
)

target_link_libraries(${PROJECT_NAME} m rtp pthread
        ${ROCKCHIPMPP_LIBRARIES}
        ${LIBDRM_LIBRARIES}
        ${LIBRGA_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${MSGPACK_LIBRARIES}
        lvgl::lvgl
)


###########################################################################
# install step

# Install to /usr/local/bin/
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

#install assets /usr/etc/msp-osd/fonts
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/
        DESTINATION etc/msp-osd/fonts
        FILES_MATCHING PATTERN "*.*")

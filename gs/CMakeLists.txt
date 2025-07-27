cmake_minimum_required(VERSION 2.8...3.13)
project(vd-link C)

set(CMAKE_C_FLAGS_DEBUG "-O1 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# LVGL configuration
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE STRING "Path to lv_conf.h dir" FORCE)

# disable LVGL examples and demos
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Disable LVGL examples" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Disable LVGL demos" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lvgl)
message(STATUS "LVGL version: ${LVGL_VERSION}")

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
        ${ZLIB_INCLUDE_DIRS}
        ${MPP_INCLUDE_DIRS}
        ${LIBDRM_INCLUDE_DIRS}
        ${LIBRGA_INCLUDE_DIRS}
        ${ROCKCHIPMPP_INCLUDE_DIRS}
        ${MSGPACK_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/lvgl
        ${CMAKE_SOURCE_DIR}/librtp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/msp-osd
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
)

target_link_libraries(${PROJECT_NAME} PRIVATE m rtp lvgl
        ${ROCKCHIPMPP_LIBRARIES}
        ${LIBDRM_LIBRARIES}
        ${LIBRGA_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${MSGPACK_LIBRARIES}
)


###########################################################################
# install step

# Install to /usr/local/bin/
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

#install assets /usr/etc/msp-osd/fonts
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/
        DESTINATION etc/msp-osd/fonts
        FILES_MATCHING PATTERN "*.*")

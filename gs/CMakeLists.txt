cmake_minimum_required(VERSION 2.8...3.13)

# TARGET = rk3566 | desktop
set(TARGET "rk3566" CACHE STRING "Build target (rk3566|desktop)")
set(SLOW_PC_MODE OFF CACHE BOOL "Enable slow PC mode (for low-end PCs)")
set(WFB_STATUS_LINK OFF CACHE BOOL "Enable WFB status link")

message(STATUS "Build TARGET: ${TARGET}")
project(vd-link-${PLATFORM} C)

set(CMAKE_C_FLAGS_DEBUG   "-O1 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# =========================
# LVGL configuration
# =========================
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE STRING "Path to lv_conf.h dir" FORCE)

set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Disable LVGL examples" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "Disable LVGL demos"    FORCE)
set(BUILD_SHARED_LIBS        OFF CACHE BOOL ""                      FORCE)
set(LVGL_ENABLE_INSTALL      OFF CACHE BOOL "Disable LVGL install"  FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lvgl)
message(STATUS "LVGL version: ${LVGL_VERSION}")

# =========================
# pkg-config + deps
# =========================
find_package(PkgConfig REQUIRED)

# Zlib (common)
pkg_check_modules(ZLIB REQUIRED zlib)

if(APPLE AND "${TARGET}" STREQUAL "desktop")
    execute_process(
            COMMAND brew --prefix
            OUTPUT_VARIABLE BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )

    if(BREW_PREFIX)
        set(_brew_pc_paths
                "${BREW_PREFIX}/lib/pkgconfig"
                "${BREW_PREFIX}/opt/sdl2/lib/pkgconfig"
                "${BREW_PREFIX}/opt/ffmpeg/lib/pkgconfig"
        )
        set(_merged_pc "$ENV{PKG_CONFIG_PATH}")
        foreach(p IN LISTS _brew_pc_paths)
            if(EXISTS "${p}")
                if(_merged_pc)
                    set(_merged_pc "${p}:${_merged_pc}")
                else()
                    set(_merged_pc "${p}")
                endif()
            endif()
        endforeach()
        if(_merged_pc)
            set(ENV{PKG_CONFIG_PATH} "${_merged_pc}")
            message(STATUS "PKG_CONFIG_PATH set to: $ENV{PKG_CONFIG_PATH}")
        endif()
    endif()
endif()

# =========================
# Platform-specific deps
# =========================
if("${TARGET}" STREQUAL "rk3566")
    pkg_check_modules(LIBRGA     REQUIRED IMPORTED_TARGET librga)
    pkg_check_modules(LIBDRM     REQUIRED IMPORTED_TARGET libdrm)
    pkg_check_modules(ROCKCHIPMPP REQUIRED IMPORTED_TARGET rockchip_mpp)
    pkg_check_modules(MSGPACK    REQUIRED IMPORTED_TARGET msgpack)
    add_definitions(-DPLATFORM_ROCKCHIP=1)
    message(STATUS "Building for rk3566 with DRM/RGA/MPP")
elseif ("${TARGET}" STREQUAL "desktop")
    find_package(SDL2 CONFIG REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVUTIL  REQUIRED libavutil)
    pkg_check_modules(SWSCALE REQUIRED libswscale)

    add_definitions(-DPLATFORM_DESKTOP=1)
    message(STATUS "Building for desktop with SDL2 + FFmpeg (libavcodec/libavutil/libswscale)")

    if (SLOW_PC_MODE)
        add_definitions(-DSLOW_PC_MODE=1)
        message(STATUS "Enabling multi thread decoding SLOW_PC_MODE libavcodec optimizations")
    endif()
else()
    message(FATAL_ERROR "Unknown TARGET: ${TARGET}. Must be 'rk3566' or 'desktop'")
endif()

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

if("${TARGET}" STREQUAL "desktop")
    message(STATUS "SDL2_INCLUDE_DIRS:       ${SDL2_INCLUDE_DIRS}")
    message(STATUS "SDL2_LIBRARIES:          ${SDL2_LIBRARIES}")
    message(STATUS "SDL2_LIBRARY_DIRS:       ${SDL2_LIBRARY_DIRS}")
    message(STATUS "AVCODEC_INCLUDE_DIRS:    ${AVCODEC_INCLUDE_DIRS}")
    message(STATUS "AVCODEC_LIBRARIES:       ${AVCODEC_LIBRARIES}")
    message(STATUS "AVCODEC_LIBRARY_DIRS:    ${AVCODEC_LIBRARY_DIRS}")
    message(STATUS "AVUTIL_INCLUDE_DIRS:     ${AVUTIL_INCLUDE_DIRS}")
    message(STATUS "AVUTIL_LIBRARIES:        ${AVUTIL_LIBRARIES}")
    message(STATUS "AVUTIL_LIBRARY_DIRS:     ${AVUTIL_LIBRARY_DIRS}")
    message(STATUS "SWSCALE_INCLUDE_DIRS:    ${SWSCALE_INCLUDE_DIRS}")
    message(STATUS "SWSCALE_LIBRARIES:       ${SWSCALE_LIBRARIES}")
    message(STATUS "SWSCALE_LIBRARY_DIRS:    ${SWSCALE_LIBRARY_DIRS}")
endif()

# =========================
# include_directories
# =========================
include_directories(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ZLIB_INCLUDE_DIRS}
        ${MPP_INCLUDE_DIRS}
        ${LIBDRM_INCLUDE_DIRS}
        ${LIBRGA_INCLUDE_DIRS}
        ${ROCKCHIPMPP_INCLUDE_DIRS}
        ${MSGPACK_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/lvgl
        ${CMAKE_SOURCE_DIR}/lib/librtp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/msp-osd
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/lang
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/fonts
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/screens
)

if("${TARGET}" STREQUAL "desktop")
    include_directories(
            ${SDL2_INCLUDE_DIRS}
            ${AVCODEC_INCLUDE_DIRS}
            ${AVUTIL_INCLUDE_DIRS}
            ${SWSCALE_INCLUDE_DIRS}
    )
endif()

# =========================
# Sources
# =========================
set(SRC_COMMON
        src/main.c
        src/rtp_receiver.c
        src/msp-osd.c
)

set(MSP_OSD_SRC
        src/msp-osd/fakehd/fakehd.c
        src/msp-osd/font/font.c
        src/msp-osd/font/unicode_utils.c
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
        src/ui/fonts/montserrat_cyrillic_14.c
        src/ui/fonts/montserrat_cyrillic_16.c
        src/ui/fonts/montserrat_cyrillic_medium_16.c
        src/ui/fonts/montserrat_cyrillic_18.c
        src/ui/fonts/montserrat_cyrillic_medium_18.c
        src/ui/fonts/montserrat_cyrillic_20.c
        src/ui/fonts/montserrat_cyrillic_medium_20.c
        src/ui/fonts/montserrat_cyrillic_22.c
        src/ui/fonts/montserrat_cyrillic_medium_22.c
        src/ui/fonts/montserrat_cyrillic_24.c
        src/ui/fonts/montserrat_cyrillic_26.c
        src/ui/fonts/montserrat_cyrillic_28.c
        src/ui/fonts/montserrat_cyrillic_30.c
        src/ui/fonts/montserrat_cyrillic_32.c
        src/ui/fonts/montserrat_cyrillic_34.c
        src/ui/fonts/montserrat_cyrillic_36.c
        src/ui/fonts/montserrat_cyrillic_38.c
        src/ui/fonts/montserrat_cyrillic_40.c
        src/ui/fonts/montserrat_cyrillic_42.c
        src/ui/fonts/montserrat_cyrillic_48.c
        src/ui/ui.c
        src/ui/lang/lang.c
        src/ui/screens/screens.c
        src/ui/screens/status.c
        src/ui/screens/settings.c
)

if("${TARGET}" STREQUAL "rk3566")
    list(APPEND SRC_COMMON
            src/drm_display.c
            src/decoder.c
    )
elseif("${TARGET}" STREQUAL "desktop")
    list(APPEND SRC_COMMON
            src/sdl2_display.c
            src/decoder_pc.c
    )
endif()

if (WFB_STATUS_LINK)
    set(WFB_STATUS_LINK_SRC
            src/wfb_status_link.c)
    add_definitions(-DWFB_STATUS_LINK=1)
    message(STATUS "Enabling WFB status link feature")
endif()

add_executable(${PROJECT_NAME}
        ${SRC_COMMON}
        ${MSP_OSD_SRC}
        ${WFB_STATUS_LINK_SRC}
)

# =========================
# Libraries
# =========================
set(COMMON_LIBS
        m
        rtp
        lvgl
        ${ZLIB_LIBRARIES}
        ${MSGPACK_LIBRARIES}
)

if("${TARGET}" STREQUAL "rk3566")
    list(APPEND COMMON_LIBS
            ${ROCKCHIPMPP_LIBRARIES}
            ${LIBDRM_LIBRARIES}
            ${LIBRGA_LIBRARIES}
    )
elseif ("${TARGET}" STREQUAL "desktop")
    list(APPEND COMMON_LIBS
            pthread
            ${SDL2_LIBRARIES}
            ${AVCODEC_LIBRARIES}
            ${AVUTIL_LIBRARIES}
            ${SWSCALE_LIBRARIES}
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${COMMON_LIBS})

if("${TARGET}" STREQUAL "desktop")
    target_link_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${AVCODEC_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
    ${SWSCALE_LIBRARY_DIRS}
    ${ZLIB_LIBRARY_DIRS})
endif()

if(APPLE AND "${TARGET}" STREQUAL "desktop")
    set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "@loader_path;/opt/homebrew/lib")
endif()

# Install to /usr/local/bin/
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

#install assets /usr/etc/msp-osd/fonts
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/
        DESTINATION etc/msp-osd/fonts
        FILES_MATCHING PATTERN "*.*")

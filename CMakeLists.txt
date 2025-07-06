cmake_minimum_required(VERSION 3.20)
project(vd-link C)

set(CMAKE_C_STANDARD 99)

add_subdirectory(librtp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(MSGPACK REQUIRED IMPORTED_TARGET rockchip_mpp)

include_directories(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MPP_INCLUDE_DIRS}
        ${LIBDRM_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/librtp/include
)

add_executable(${PROJECT_NAME}
        src/main.c
        src/drm_display.c
        src/rtp_receiver.c
        src/decoder.c
)

target_link_libraries(${PROJECT_NAME} rockchip_mpp drm rtp rga)


###########################################################################
# install step

# Install to /usr/local/bin/
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

project(fc_conn)

set(CMAKE_C_STANDARD 99)

add_library(fc_conn STATIC
    # msp.c
    fc_conn.c
    msp_interface.c
)

target_link_libraries(${PROJECT_NAME} msp pthread)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
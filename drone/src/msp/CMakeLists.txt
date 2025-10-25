project(msp)

set(CMAKE_C_STANDARD 99)

add_library(fc_msp STATIC
    msp.c
    fc_conn.c
    msp_interface.c
)

target_include_directories(msp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
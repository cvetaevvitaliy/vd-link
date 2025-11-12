project(detection)

project(rtp_streamer C CXX)
set(CMAKE_C_STANDARD 99)


add_library(detection STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/detection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/detection_stream.c
        ${CMAKE_CURRENT_SOURCE_DIR}/rknn_npu.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/postprocess.cpp
)
target_include_directories(detection
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(detection
        PRIVATE
         ${CMAKE_SOURCE_DIR}/lib/bytetrack
)

target_link_libraries(detection
        rknn_api
        rga
        m
        bytetrack_wrapper
)

install(FILES ../assets/ai_model/default_anchors.txt
        DESTINATION /etc/ai_model/
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

install(FILES ../assets/ai_model/yolov7_tiny.rknn
        DESTINATION /etc/ai_model/
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

cmake_minimum_required(VERSION 3.16)

# Nanopb is required - no fallback
find_package(Nanopb REQUIRED)

# Generate nanopb sources from proto files
nanopb_generate_cpp(TARGET proto RELPATH proto
    proto/simple.proto proto/sub/unlucky.proto)

# Create link library with protobuf support
add_library(link STATIC link.c)
target_link_libraries(link PUBLIC proto)
target_include_directories(link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Optional example
option(BUILD_PROTO_EXAMPLE "Build protobuf example" OFF)
if(BUILD_PROTO_EXAMPLE)
    add_executable(proto_example example.c)
    target_link_libraries(proto_example link)
endif()
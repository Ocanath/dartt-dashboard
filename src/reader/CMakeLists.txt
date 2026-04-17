add_library(dartt_link STATIC
    dartt_link.cpp
)

target_include_directories(dartt_link PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/dartt-protocol/src
    ${CMAKE_SOURCE_DIR}/external/byte-stuffing/C-COBS
    ${CMAKE_SOURCE_DIR}/external/serial-cross-platform
)

target_link_libraries(dartt_link PUBLIC
    dartt_protocol
    cobs
    serial
)

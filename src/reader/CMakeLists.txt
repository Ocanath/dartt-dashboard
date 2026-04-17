add_library(dartt_reader STATIC
    dartt_reader.cpp
)

target_include_directories(dartt_reader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/dartt-protocol/src
    ${CMAKE_SOURCE_DIR}/external/byte-stuffing/C-COBS
    ${CMAKE_SOURCE_DIR}/external/serial-cross-platform
)

target_link_libraries(dartt_reader PUBLIC
    dartt_protocol
    cobs
    serial
)

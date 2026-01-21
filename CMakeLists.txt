cmake_minimum_required(VERSION 3.22) # Dawn requires CMake >= 3.22
project(dartt_dashboard C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# External Dependencies
# ============================================================================

# SDL2 (system)
find_package(SDL2 REQUIRED)

# OpenGL
find_package(OpenGL REQUIRED)

# ImGui (no CMakeLists.txt, add manually)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES} OpenGL::GL)

# byte-stuffing (provides: cobs, PPP)
add_subdirectory(external/byte-stuffing)

# serial-cross-platform (provides: serial)
add_subdirectory(external/serial-cross-platform)

# dartt-protocol (provides: dartt_protocol, dartt_checksum)
add_subdirectory(external/dartt-protocol)

# nlohmann/json (provides: nlohmann_json::nlohmann_json)
add_subdirectory(external/json)

# ============================================================================
# Main Application
# ============================================================================

add_executable(dartt_dashboard
    src/main.cpp
)

target_link_libraries(dartt_dashboard PRIVATE
    imgui
    cobs
    PPP
    serial
    dartt_protocol
    dartt_checksum
    nlohmann_json::nlohmann_json
)

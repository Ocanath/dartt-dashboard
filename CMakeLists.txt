cmake_minimum_required(VERSION 3.22) # Dawn requires CMake >= 3.22
project(dartt_dashboard C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# External Dependencies
# ============================================================================



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

# libdwarf (provides: dwarf)
# Disable components we don't need
set(BUILD_DWARFDUMP OFF CACHE BOOL "" FORCE)
set(BUILD_DWARFGEN OFF CACHE BOOL "" FORCE)
set(BUILD_DWARFEXAMPLE OFF CACHE BOOL "" FORCE)
set(DO_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_NON_SHARED ON CACHE BOOL "" FORCE)
add_subdirectory(external/libdwarf)

# ELFIO (header-only library for ELF symbol table access)
# No build step required, just include directory

# ============================================================================
# Main Application
# ============================================================================

add_executable(${PROJECT_NAME}
    src/main.cpp
	src/dartt_init.cpp
	src/config.cpp
	src/elf_parser.cpp
	src/ui.cpp
	src/buffer_sync.cpp
	src/plotting.cpp
	src/colors.cpp
)

# Debug symbols
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2>
)

# SDL2 (system)
# SDL2 Setup
if(WIN32)
    # Path to your local SDL2 copy
    set(SDL2_DIR "${CMAKE_SOURCE_DIR}/external/SDL")

    # Determine the target architecture
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit architecture
        set(SDL2_LIB_DIR "${SDL2_DIR}/lib/x64")
    else()
        # 32-bit architecture
        set(SDL2_LIB_DIR "${SDL2_DIR}/lib/x86")
    endif()

    # Include SDL2 headers
    include_directories(${SDL2_DIR}/include)
    
    # Link SDL2 libraries
	target_link_libraries(${PROJECT_NAME}
		${SDL2_LIB_DIR}/SDL2main.lib
		${SDL2_LIB_DIR}/SDL2.lib
	)

else()
    # Find SDL2 on other platforms
    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIRS})
	target_link_libraries(${PROJECT_NAME}	
		${SDL2_LIBRARIES})

endif()

# Include directories for ELF parsing
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/ELFIO
    ${CMAKE_CURRENT_SOURCE_DIR}/external/libdwarf/src/lib/libdwarf
)

target_link_libraries(${PROJECT_NAME}
    imgui
    cobs
    PPP
    serial
    dartt_protocol
    dartt_checksum
    nlohmann_json::nlohmann_json
    dwarf
)

# tinycsocket socket libraries (vendored, header-only with TINYCSOCKET_IMPLEMENTATION)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} wsock32 ws2_32 iphlpapi)
endif()

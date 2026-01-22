#include <cstdio>

// Platform headers (must come before GL on Windows)
#ifdef _WIN32
#include <windows.h>
#endif

// SDL2
#include <SDL.h>

// OpenGL (optional - imgui backend handles this, but needed if you call GL directly)
#include <GL/gl.h>

// ImGui
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

// byte-stuffing
#include "cobs.h"
#include "PPP.h"

// serial-cross-platform
#include "serial.h"

// dartt-protocol
#include "dartt.h"
#include "dartt_sync.h"
#include "checksum.h"

// JSON
#include <nlohmann/json.hpp>
#include "config.h"

int main(int argc, char* argv[]) 
{
    (void)argc;
    (void)argv;
    printf("dartt-dashboard\n");
    return 0;
}

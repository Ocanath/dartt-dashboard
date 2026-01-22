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
#include "dartt_init.h"

int main(int argc, char* argv[]) 
{
    (void)argc;
    (void)argv;
	
	int rc = serial.autoconnect(921600);//todo - add baudrate as an argument
	if(rc != true)
	{
		printf("Warning - no serial connection made\n");
	}
	
	dartt_sync_t ds;
	init_ds(&ds);	//basic
	ds.address = 5;
	int bufsize = 10;	//obtain from json
	
	//assign ctl buf and periph buf
	ds.ctl_base.buf = (unsigned char *)calloc(1, bufsize);
	ds.ctl_base.len = bufsize;
	ds.ctl_base.size = bufsize;	//set up the ctl by default to read the whole thing
	ds.periph_base.buf = (unsigned char *)calloc(1, bufsize);
	ds.periph_base.len = bufsize;
	ds.periph_base.size = bufsize;

	rc = dartt_read_multi(&ds.ctl_base, &ds);
	printf("Read: got %d\n", rc);

	printf("Done\n");
    return 0;
}

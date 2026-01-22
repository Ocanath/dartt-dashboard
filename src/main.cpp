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

#include <fstream>

using json = nlohmann::json;


int main(int argc, char* argv[]) 
{

	//The window we'll be rendering to
	SDL_Window* window = NULL;

	SDL_Color bgColor = { 10, 10, 10, 255 };

	//Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
	}
	else
	{
		printf("sdl init success\n");
	}
	
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



	json j;
	std::ifstream fhandle("config.json");
	if(fhandle.is_open() == 0)
	{
		printf("No config found\n");
		return -1;
	}
	j = json::parse(fhandle);
	printf("%s\n", j.value("symbol",""));


	printf("Done\n");
    return 0;
}

#include <cstdio>

// Platform headers (must come before GL on Windows)
#ifdef _WIN32
#include <windows.h>
#endif

// SDL2
#include <SDL.h>

// OpenGL
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

// App
#include "config.h"
#include "dartt_init.h"
#include "ui.h"
#include "buffer_sync.h"

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) 
	{
		printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
		return -1;
	}

	// GL attributes
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	// Create window
	SDL_Window* window = SDL_CreateWindow(
		"DARTT Dashboard",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		1280, 720,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
	);
	
	if (!window) 
	{
		printf("Window creation failed: %s\n", SDL_GetError());
		return -1;
	}

	// Create GL context
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	if (!gl_context) 
	{
		printf("GL context creation failed: %s\n", SDL_GetError());
		return -1;
	}
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1); // VSync

	// Initialize ImGui
	if (!init_imgui(window, gl_context)) 
	{
		printf("ImGui initialization failed\n");
		return -1;
	}

	// Serial connection
	int rc = serial.autoconnect(921600);
	if (rc != true) 
	{
		printf("Warning - no serial connection made\n");
	}

	// Load config
	DarttConfig config;
	if (!load_dartt_config("config.json", config)) 
	{
		printf("Failed to load config.json\n");
		// Continue anyway - UI will be empty
	}

	// Allocate DARTT buffers
	if (config.nbytes > 0) 
	{
		config.allocate_buffers();
	}

	// Setup dartt_sync
	dartt_sync_t ds;
	init_ds(&ds);
	ds.address = 0x05; // TODO: make configurable

	if (config.ctl_buf.buf && config.periph_buf.buf) 
	{
		//shallow copy the buffers
		ds.ctl_base.buf = config.ctl_buf.buf;
		ds.ctl_base.len = config.ctl_buf.len;
		ds.ctl_base.size = config.ctl_buf.size;
		ds.periph_base.buf = config.periph_buf.buf;
		ds.periph_base.len = config.periph_buf.len;
		ds.periph_base.size = config.periph_buf.size;
	}

	// Main loop
	bool running = true;
	while (running)
	{
		// Poll events
		SDL_Event event;
		while (SDL_PollEvent(&event)) 
		{
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) 
			{
				running = false;
			}
			if (event.type == SDL_WINDOWEVENT &&
				event.window.event == SDL_WINDOWEVENT_CLOSE &&
				event.window.windowID == SDL_GetWindowID(window)) 
			{
				running = false;
			}
		}

		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		// Render UI
		bool value_edited = render_live_expressions(config);
		(void)value_edited; // Used for debugging if needed

		// WRITE: Send dirty fields to device
		if (config.ctl_buf.buf && config.periph_buf.buf) {
			std::vector<MemoryRegion> write_queue = build_write_queue(config);
			for (MemoryRegion& region : write_queue) {
				sync_fields_to_ctl_buf(config, region);

				buffer_t slice = {
					.buf = config.ctl_buf.buf + region.start_offset,
					.size = region.length,
					.len = region.length
				};

				int rc = dartt_write_multi(&slice, &ds);
				if (rc == DARTT_PROTOCOL_SUCCESS) {
					clear_dirty_flags(region);
					printf("write ok: offset=%u len=%u\n", region.start_offset, region.length);
				} else {
					printf("write error %d\n", rc);
				}
			}
		}

		// READ: Poll subscribed fields from device
		if (config.ctl_buf.buf && config.periph_buf.buf)
		{
			std::vector<MemoryRegion> read_queue = build_read_queue(config);
			for (MemoryRegion& region : read_queue) 
			{
				buffer_t slice = 
				{
					.buf = config.ctl_buf.buf + region.start_offset,
					.size = region.length,
					.len = region.length
				};


				int rc = dartt_read_multi(&slice, &ds);
				if (rc == DARTT_PROTOCOL_SUCCESS) 
				{
					sync_periph_buf_to_fields(config, region);
				} 
				else 
				{
					printf("read error %d\n", rc);
				}
			}
		}

		// Render
		ImGui::Render();
		int display_w, display_h;
		SDL_GetWindowSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(window);
	}

	// Save UI settings back to config
	// save_dartt_config("config.json", config);

	// Cleanup
	shutdown_imgui();
	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}

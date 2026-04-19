#include <cstdio>

#define TINYCSOCKET_IMPLEMENTATION

// Platform headers (must come before GL on Windows)
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#endif

// tinycsocket (must come before SDL - SDL redefines main to SDL_main)
#include "tinycsocket.h"

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
#include "dartt_crc.h"
#include "dartt_link.h"


// App
#include "config.h"
#include "ui.h"
#include "buffer_sync.h"
#include "plotting.h"
#include "elf_parser.h"

#include <algorithm>
#include <cstring>
#include <string>

struct ReadCallbackCtx {
    DarttConfig* config;
    Plotter*     plot;
    DarttLink*   dl;
};

static void on_read_reply(const dartt_mem_t* periph, void* ctx)
{
    ReadCallbackCtx* c = (ReadCallbackCtx*)ctx;

    {
        std::lock_guard<std::mutex> lock(c->dl->periph_buf_mutex);
        for (int i = 0; i < (int)c->config->subscribed_list.size(); i++)
        {
            DarttField* field = c->config->subscribed_list[i];
            std::memcpy(&field->value.u8,
                        c->config->periph_buf.buf + field->byte_offset,
                        field->nbytes);
        }
        calculate_display_values(c->config->leaf_list);
    }

    {
        std::lock_guard<std::mutex> lock(c->plot->plot_mutex);
        for (int i = 0; i < (int)c->plot->lines.size(); i++)
            c->plot->lines[i].enqueue_data(c->plot->window_width);
    }
}

// Helper: case-insensitive extension check
static bool ends_with_ci(const std::string& str, const std::string& suffix) 
{
	if (suffix.size() > str.size()) 
	{
		return false;
	}
	std::string tail = str.substr(str.size() - suffix.size());
	std::transform(tail.begin(), tail.end(), tail.begin(), ::tolower);
	return tail == suffix;
}


int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	// Drag-and-drop state
	std::string dropped_file_path;
	bool show_elf_popup = false;
	char var_name_buf[128] = "";
	std::string elf_load_error;
	bool pending_json_load = false;
	std::string config_json_path = "";

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

	Plotter plot;
	int width = 0;
	int height = 0;
	SDL_GetWindowSize(window, &width, &height);
	plot.init(width, height);
	
	
	if (tcs_lib_init() != TCS_SUCCESS)
	{
		printf("Failed to initialize tinycsocket\n");
	}
	else
	{
		printf("Initialize tinycsocket library success\n");
	}



	// Load config (including plotting config)
	DarttConfig config;

	// Allocate DARTT buffers
	if (config.nbytes > 0) 
	{
		config.allocate_buffers();
	}

	// Serial connection
	DarttLink dl(config.ctl_buf, config.periph_buf);

	static ReadCallbackCtx cb_ctx = { &config, &plot, &dl };
	dl.set_read_reply_callback(on_read_reply, &cb_ctx);

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
			if (event.type == SDL_DROPFILE)
			{
				char* file = event.drop.file;
				dropped_file_path = file;
				SDL_free(file);

				if (ends_with_ci(dropped_file_path, ".elf"))
				{
					var_name_buf[0] = '\0';
					elf_load_error.clear();
					show_elf_popup = true;
				}
				else if (ends_with_ci(dropped_file_path, ".json"))
				{
					pending_json_load = true;
				}
			}
		}

		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		// --- Drag-and-drop: JSON load ---
		if (pending_json_load)
		{
			pending_json_load = false;
			dl.stop();
			// Detach external references before replacing config
			for (size_t i = 0; i < plot.lines.size(); i++)
			{
				plot.lines[i].xsource = &plot.sys_sec;
				plot.lines[i].ysource = nullptr;
			}
			dl.ctl_base.buf = nullptr;
			dl.periph_base.buf = nullptr;
			config = DarttConfig();

			if (load_dartt_config(dropped_file_path.c_str(), config, plot, dl.serial, dl))
			{
				if (config.nbytes > 0)
				{
					config.allocate_buffers();
					dl.ctl_base.buf = config.ctl_buf.buf;
					dl.ctl_base.size = config.ctl_buf.size;
					dl.periph_base.buf = config.periph_buf.buf;
					dl.periph_base.size = config.periph_buf.size;
				}
				config_json_path = dropped_file_path;
				dl.start();
				printf("Loaded config from JSON: %s\n", dropped_file_path.c_str());
			}
			else
			{
				printf("Failed to load JSON: %s\n", dropped_file_path.c_str());
			}
		}

		// --- Drag-and-drop: ELF popup + load ---
		if (render_elf_load_popup(&show_elf_popup, dropped_file_path, var_name_buf, sizeof(var_name_buf), elf_load_error))
		{
			dl.stop();
			// User clicked Load - detach external references
			for (size_t i = 0; i < plot.lines.size(); i++)
			{
				plot.lines[i].xsource = &plot.sys_sec;
				plot.lines[i].ysource = nullptr;
			}
			dl.ctl_base.buf = nullptr;
			dl.periph_base.buf = nullptr;
			config = DarttConfig();

			elf_parse_error_t err = elf_parser_load_config(dropped_file_path.c_str(), var_name_buf, &config);

			if (err == ELF_PARSE_SUCCESS)
			{
				if (config.nbytes > 0)
				{
					config.allocate_buffers();
					dl.ctl_base.buf = config.ctl_buf.buf;
					dl.ctl_base.size = config.ctl_buf.size;
					dl.periph_base.buf = config.periph_buf.buf;
					dl.periph_base.size = config.periph_buf.size;
				}
				config_json_path = dropped_file_path.substr(0, dropped_file_path.size() - 4) + ".json";
				elf_parser_ctx tmp_parser;
				if (elf_parser_init(&tmp_parser, dropped_file_path.c_str()) == ELF_PARSE_SUCCESS)
				{
					elf_parser_generate_json(&tmp_parser, var_name_buf, config_json_path.c_str());
					elf_parser_cleanup(&tmp_parser);
				}
				elf_load_error.clear();
				ImGui::CloseCurrentPopup();
				dl.start();
				printf("Loaded config from ELF: %s (symbol: %s)\n",
				       dropped_file_path.c_str(), var_name_buf);
			}
			else
			{
				elf_load_error = elf_parse_error_str(err);
			}
		}

		// Rebuild subscribed and dirty lists before read/write operations
		collect_subscribed_fields(config.leaf_list, config.subscribed_list);
		collect_dirty_fields(config.leaf_list, config.dirty_list);

		// WRITE: Send dirty fields to device
		if (config.ctl_buf.buf && config.periph_buf.buf)
		{
			std::vector<MemoryRegion> write_queue = build_write_queue(config);
			for (int i = 0; i < (int)write_queue.size(); i++)
			{
				sync_fields_to_ctl_buf(config, write_queue[i]);
				dartt_mem_t slice = {
					.buf  = config.ctl_buf.buf + write_queue[i].start_offset,
					.size = write_queue[i].length
				};
				int rc = dl.enqueue_writes(slice);
				if (rc == DARTT_PROTOCOL_SUCCESS)
				{
					clear_dirty_flags(write_queue[i]);
					printf("write enqueued: offset=%u len=%u\n", write_queue[i].start_offset, write_queue[i].length);
				}
				else
				{
					printf("write error %d\n", rc);
				}
			}
		}

		// SUBSCRIBE: Rebuild DarttLink read request list when subscriptions change
		static std::vector<DarttField*> prev_subscribed;
		if (config.ctl_buf.buf && config.periph_buf.buf)
		{
			if (config.subscribed_list != prev_subscribed)
			{
				prev_subscribed = config.subscribed_list;
				std::vector<MemoryRegion> read_regions = build_read_queue(config);
				dl.clear_subscriptions();
				for (int i = 0; i < (int)read_regions.size(); i++)
				{
					dartt_mem_t region = {
						.buf  = config.ctl_buf.buf + read_regions[i].start_offset,
						.size = read_regions[i].length
					};
					dl.subscribe_region(region);
				}
				dl.build_read_requests();
			}
		}

		// Render UI
		SDL_GetWindowSize(window, &plot.window_width, &plot.window_height);
		plot.sys_sec = (float)(((double)SDL_GetTicks64())/1000.);
		{
			std::lock_guard<std::mutex> lock(dl.periph_buf_mutex);
			bool value_edited = render_live_expressions(config, plot, config_json_path, dl);
			(void)value_edited;
			render_plotting_menu(plot, config.root, config.subscribed_list);
		}
		

		// Render
		ImGui::Render();
		int display_w, display_h;
		SDL_GetWindowSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		plot.render();	//must position here
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(window);
	}

	// Save UI settings back to config
	// save_dartt_config("config.json", config);

	dl.stop();

	// Cleanup
	shutdown_imgui();
	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}

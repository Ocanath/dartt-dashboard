#ifndef DARTT_UI_H
#define DARTT_UI_H

#include <SDL.h>
#include "config.h"

// Initialize ImGui (call after SDL/OpenGL setup)
bool init_imgui(SDL_Window* window, SDL_GLContext gl_context);

// Shutdown ImGui
void shutdown_imgui();

// Render the live expressions panel
// Returns true if any value was edited (triggers write)
bool render_live_expressions(DarttConfig& config);

// Helper: recursively set subscribed state on field and all children
void set_subscribed_recursive(DarttField& field, bool subscribed);

// Helper: check if any child is subscribed (for mixed state display)
bool any_child_subscribed(const DarttField& field);

// Helper: check if all children are subscribed
bool all_children_subscribed(const DarttField& field);

#endif // DARTT_UI_H

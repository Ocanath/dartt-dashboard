#ifndef DARTT_UI_H
#define DARTT_UI_H

#include <SDL.h>
#include "config.h"
#include "plotting.h"

// Initialize ImGui (call after SDL/OpenGL setup)
bool init_imgui(SDL_Window* window, SDL_GLContext gl_context);

// Shutdown ImGui
void shutdown_imgui();

// Render the live expressions panel
// Returns true if any value was edited (triggers write)
bool render_live_expressions(DarttConfig& config);

// Render the plot settings menu with tree selectors for X/Y sources
bool render_plotting_menu(Plotter &plot, DarttField& root, const std::vector<DarttField*> &subscribed_list);

// Helper: set subscribed state on field and all children (iterative)
void set_subscribed_all(DarttField* root, bool subscribed);

// Helper: check if any child is subscribed (for mixed state display)
bool any_child_subscribed(const DarttField* root);

// Helper: check if all children are subscribed
bool all_children_subscribed(const DarttField* root);

void calculate_display_values(const std::vector<DarttField*> &leaf_list);

#endif // DARTT_UI_H

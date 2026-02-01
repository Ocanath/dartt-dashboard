#ifndef PLOTTING_H
#define PLOTTING_H

#include <SDL.h>
#include <vector>

struct fpoint_t
{
	float x;
	float y;

	fpoint_t();
	fpoint_t(float x_val, float y_val);
};

class Line
{
public:
	std::vector<fpoint_t> points;

	Line();
	Line(int capacity);

	void resize(int capacity);
	int size();
	fpoint_t* data();
};

class Plotter
{
public:
	SDL_Window* window;
	SDL_Renderer* renderer;
	int screen_width;
	int screen_height;
	int num_widths;
	std::vector<Line> lines;

	Plotter();

	// Initialize the plot with a window, existing renderer, and number of screen widths for buffer
	bool init(SDL_Window* win, SDL_Renderer* rend, int n_widths);

	// Render all lines
	void render();
};

#endif // DARTT_PLOTTING_H

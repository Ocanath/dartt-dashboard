#include <SDL.h>
#include <vector>
#include "plotting.h"

// fpoint_t definition
fpoint_t::fpoint_t()
	: x(0.0f)
	, y(0.0f)
{
}

fpoint_t::fpoint_t(float x_val, float y_val)
	: x(x_val)
	, y(y_val)
{
}

// Line class implementation
Line::Line()
	: points()
{
}

Line::Line(int capacity)
	: points(capacity)
{
}

void Line::resize(int capacity)
{
	points.resize(capacity);
}

int Line::size()
{
	return (int)points.size();
}

fpoint_t* Line::data()
{
	return points.data();
}

// Plot class implementation
Plotter::Plotter()
	: window(NULL)
	, renderer(NULL)
	, screen_width(0)
	, screen_height(0)
	, num_widths(1)
	, lines()
{
}

bool Plotter::init(SDL_Window* win, SDL_Renderer* rend, int n_widths)
{
	if (win == NULL || rend == NULL)
	{
		return false;
	}

	window = win;
	renderer = rend;
	num_widths = n_widths;

	SDL_GetWindowSize(window, &screen_width, &screen_height);

	int line_capacity = screen_width * num_widths;

	// Initialize with one line
	lines.resize(1);
	lines[0].resize(line_capacity);

	return true;
}

void Plotter::render()
{
	// TODO: implement rendering
}

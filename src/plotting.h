#ifndef PLOTTING_H
#define PLOTTING_H

#include <vector>
#include <cstdint>

struct fpoint_t
{
	float x;
	float y;

	fpoint_t();
	fpoint_t(float x_val, float y_val);
};

struct color_t
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;

	color_t();
	color_t(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
};

class Line
{
public:
	std::vector<fpoint_t> points;
	color_t color;

	Line();
	Line(int capacity);

	void resize(int capacity);
	int size();
	fpoint_t* data();
};

class Plotter
{
public:
	int window_width;
	int window_height;
	float xscale;
	int num_widths;
	std::vector<Line> lines;

	Plotter();

	// Initialize the plotter with dimensions and number of widths for buffer
	bool init(int width, int height);

	// Render all lines directly to OpenGL framebuffer
	void render();
};

#endif // PLOTTING_H

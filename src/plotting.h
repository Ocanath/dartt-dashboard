#ifndef PLOTTING_H
#define PLOTTING_H

#include <vector>
#include <cstdint>
#include "colors.h"

struct fpoint_t
{
	float x;
	float y;

	fpoint_t();
	fpoint_t(float x_val, float y_val);
};


typedef enum {TIME_MODE, XY_MODE}timemode_t;

class Line
{
public:
	std::vector<fpoint_t> points;
	rgb_t color;



	float * xsource;	//pointer to the x variable which we source for our data stream
	float * ysource;	//pointer to the y variable which we source for our data stream

	/*
		Data is formatted and 
	*/
	timemode_t mode;

	//formula: display_value*xscale + xoffset (for xy mode)
	//for time mode: xscale is computed automatically
	float xscale;
	float xoffset;	//unused in time mode (roll mode). In xy-mode, used to manipulate the xy offset in display_ units


	//formula: display_value*yscale + yoffset - pixel units (always)
	float yscale;	//scale the display_ value by one additional scalar  for plotting
	float yoffset;

	//queue size
	uint32_t enqueue_cap;

	Line();
	Line(int capacity);

	bool enqueue_data(int screen_width);
	
};

class Plotter
{
public:
	int window_width;
	int window_height;

	int num_widths;
	std::vector<Line> lines;

	Plotter();

	// Initialize the plotter with dimensions and number of widths for buffer
	bool init(int width, int height);

	float sys_sec;	//global time

	// Render all lines directly to OpenGL framebuffer
	void render();
};

#endif // PLOTTING_H

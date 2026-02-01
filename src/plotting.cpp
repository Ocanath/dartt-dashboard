#include <GL/gl.h>
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

// color_t definition
color_t::color_t()
	: r(255)
	, g(255)
	, b(255)
	, a(255)
{
}

color_t::color_t(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
	: r(red)
	, g(green)
	, b(blue)
	, a(alpha)
{
}

// Line class implementation
Line::Line()
	: points()
	, color()
{
}

Line::Line(int capacity)
	: points(capacity)
	, color()
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

// Plotter class implementation
Plotter::Plotter()
	: plot_width(0)
	, plot_height(0)
	, num_widths(1)
	, lines()
{
}

bool Plotter::init(int width, int height, int n_widths)
{
	if (width <= 0 || height <= 0)
	{
		return false;
	}

	plot_width = width;
	plot_height = height;
	num_widths = n_widths;

	int line_capacity = plot_width * num_widths;

	// Initialize with one line
	lines.resize(1);
	lines[0].resize(line_capacity);

	return true;
}

void Plotter::render()
{
	// Save current matrix state
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, plot_width, 0, plot_height, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// Draw each line
	for (int i = 0; i < (int)lines.size(); i++)
	{
		Line* line = &lines[i];
		int num_points = line->size();

		if (num_points < 2)
		{
			continue;
		}

		glColor4ub(line->color.r, line->color.g, line->color.b, line->color.a);
		glBegin(GL_LINE_STRIP);
		for (int j = 0; j < num_points; j++)
		{
			glVertex2f(line->points[j].x, line->points[j].y);
		}
		glEnd();
	}

	// Restore matrix state
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
}

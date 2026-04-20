#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
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


// Line class implementation
Line::Line()
	: points()
	, color()
	, xsource(NULL)
	, ysource(NULL)
	, mode(TIME_MODE)
	, xscale(1.f)
	, xoffset(0.f)
	, yscale(1.f)
	, yoffset(0.f)
	, enqueue_cap(2000)
	, head_(0)
	, count_(0)
{
	points.resize(enqueue_cap);
	color.r = 0;
	color.g = 0;
	color.b = 0;
	color.a = 0xFF;
}

Line::Line(int capacity)
	: points()
	, color()
	, xsource(NULL)
	, ysource(NULL)
	, mode(TIME_MODE)
	, xscale(1.f)
	, xoffset(0.f)
	, yscale(1.f)
	, yoffset(0.f)
	, enqueue_cap((uint32_t)capacity)
	, head_(0)
	, count_(0)
{
	points.resize(enqueue_cap);
	color.r = 0;
	color.g = 0;
	color.b = 0;
	color.a = 0xFF;
}

// Plotter class implementation
Plotter::Plotter()
	: window_width(0)
	, window_height(0)
	, num_widths(1)
	, lines()
	, sys_usec(0.0f)
{
}

bool Plotter::init(int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		return false;
	}

	window_width = width;
	window_height = height;
	int line_capacity = 0;

	// Initialize with one line
	lines.resize(1);
	lines[0].xsource = &sys_usec;
	int color_idx = (lines.size() % NUM_COLORS);
	lines[0].color = template_colors[color_idx];
	return true;
}

int sat_pix_to_window(int val, int thresh)
{
	if(val < 1)
	{
		return 1;
	}
	else if(val > (thresh-1))
	{
		return (thresh-1);
	}
	return val;
}

void Plotter::render()
{
	std::lock_guard<std::mutex> lock(plot_mutex);
	// Save current matrix state
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, window_width, 0, window_height, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// Draw each line
	for (int i = 0; i < (int)lines.size(); i++)
	{
		Line* line = &lines[i];
		size_t num_points = line->count_;

		if (num_points < 2)
		{
			continue;
		}

		fpoint_t& oldest = line->points[line->head_];
		glColor4ub(line->color.r, line->color.g, line->color.b, line->color.a);
		glBegin(GL_LINE_STRIP);
		for (size_t j = 0; j < num_points; j++)
		{
			fpoint_t& pt = line->points[(line->head_ + j) % line->enqueue_cap];
			int x = 0;
			int y = 0;
			if (line->mode == TIME_MODE)
			{
				x = (int)((pt.x - oldest.x) * line->xscale);
				y = (int)(pt.y * line->yscale + line->yoffset + (float)window_height / 2.f);
			}
			else
			{
				x = (int)(pt.x * line->xscale + line->xoffset + (float)window_width / 2.f);
				y = (int)(pt.y * line->yscale + line->yoffset + (float)window_height / 2.f);
			}
			x = sat_pix_to_window(x, window_width);
			y = sat_pix_to_window(y, window_width);
			glVertex2f(x, y);
		}
		glEnd();
	}

	// Restore matrix state
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
}



bool Line::enqueue_data(int screen_width)
{
	if(xsource == NULL || ysource == NULL)
	{
		return false;	//fail due to bad pointer reference
	}
	if (points.size() != enqueue_cap)
	{
		points.resize(enqueue_cap);
		head_ = 0;
		count_ = 0;
	}
	size_t tail = (head_ + count_) % enqueue_cap;
	points[tail] = fpoint_t(*xsource, *ysource);
	if (count_ < enqueue_cap)
		count_++;
	else
		head_ = (head_ + 1) % enqueue_cap;

	if(mode == TIME_MODE)
	{
		if(count_ >= 2)
		{
			fpoint_t& oldest = points[head_];
			fpoint_t& newest = points[(head_ + count_ - 1) % enqueue_cap];
			float div = newest.x - oldest.x;
			if(div > 0)
			{
				xscale = screen_width / div;
			}
			else if(div < 0)
			{
				head_ = 0;
				count_ = 0;
			}
		}
	}
	return true;
}

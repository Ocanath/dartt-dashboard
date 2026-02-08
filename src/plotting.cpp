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
{
	color.r = 0;
	color.g = 0;
	color.b = 0;
	color.a = 0xFF;
}

Line::Line(int capacity)
	: points(capacity)
	, color()
	, xsource(NULL)
	, ysource(NULL)
	, mode(TIME_MODE)
	, xscale(1.f)
	, xoffset(0.f)
	, yscale(1.f)
	, yoffset(0.f)
	, enqueue_cap(2000)
{
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
	, sys_sec(0.0f)
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
	lines[0].points.resize(line_capacity);
	lines[0].points.clear();
	lines[0].xsource = &sys_sec;
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
		int num_points = line->points.size();

		if (num_points < 2)
		{
			continue;
		}

		glColor4ub(line->color.r, line->color.g, line->color.b, line->color.a);	
		glBegin(GL_LINE_STRIP);
		for (int j = 0; j < num_points; j++)
		{
			int x = 0; 
			int y = 0;
			if(line->mode == TIME_MODE)
			{
				x = (int)( (line->points[j].x - line->points.front().x) * line->xscale);
				y = (int)(line->points[j].y * line->yscale + line->yoffset + (float)window_height/2.f);	
			}
			else
			{
				x = (int)(line->points[j].x * line->xscale + line->xoffset + (float)window_width/2.f);
				y = (int)(line->points[j].y * line->yscale + line->yoffset + (float)window_height/2.f);
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
	//enqueue data
	if(points.size() < enqueue_cap)	//cap on buffer width - may want to expand
	{
		points.push_back(fpoint_t(*xsource, *ysource));
	}	
	else if(points.size() > enqueue_cap)
	{
		points.resize(enqueue_cap);
	}
	if(points.size() == enqueue_cap)
	{
		std::rotate(points.begin(), points.begin() + 1, points.end());
		points.back() = fpoint_t(*xsource, *ysource);
	}

	if(mode == TIME_MODE)
	{
		//THEN calculate xscale based on current buffer state
		if(points.size() >= 2)
		{
			float div = points.back().x - points.front().x;
			if(div > 0)
			{
				xscale = screen_width/div;
			}
			else if(div < 0) //decreasing time
			{
				points.clear();	//this just sets size=0 - can preallocate and clear for speed
			}	
		}	
	}
	return true;
}

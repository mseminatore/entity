#pragma once

// Component definitions
#include <string>

struct Position
{
	float x;
	float y;
};

struct Velocity
{
	float vx;
	float vy;
};

struct Radius
{
	float r;
};

struct Health
{
	float hp;
};

struct Bounds
{
	float width;
	float height;
};

struct Name
{
	std::string value;
};
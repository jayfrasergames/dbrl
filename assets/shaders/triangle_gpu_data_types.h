#ifndef TRIANGLE_GPU_DATA_TYPES_H
#define TRIANGLE_GPU_DATA_TYPES_H

#include "shader_global_constants.h"

/*
struct Triangle_Constants
{

};
*/

struct Triangle_Instance
{
	F2 a;
	F2 b;
	F2 c;

	F4 color;
};

#endif
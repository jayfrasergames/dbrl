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
	F2 a        SEMANTIC_NAME(VERTEX_A);
	F2 b        SEMANTIC_NAME(VERTEX_B);
	F2 c        SEMANTIC_NAME(VERTEX_C);

	F4 color    SEMANTIC_NAME(COLOR);
};

#endif
#ifndef PARTICLES_GPU_DATA_TYPES_H
#define PARTICLES_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

#define PARTICLES_WIDTH 64

struct Particles_Constant_Buffer
{
	F2 tile_size;
	F1 time;
	F1 _dummy;
};

struct Particle_Instance
{
	F1 start_time;
	F1 end_time;
	F2 start_pos;

	F4 start_color;

	F4 end_color;

	F2 start_velocity;
	F2 acceleration;

	F2 sin_inner_coeff;
	F2 sin_outer_coeff;

	F2 sin_phase_offset;
};

#endif

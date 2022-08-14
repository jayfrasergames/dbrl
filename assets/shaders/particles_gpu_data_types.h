#ifndef PARTICLES_GPU_DATA_TYPES_H
#define PARTICLES_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

#define PARTICLES_WIDTH 64

struct Particles_Constant_Buffer
{
	F2 tile_size;
	F2 world_size;

	F1 time;
	F3 _dummy;
};

struct Particle_Instance
{
	F1 start_time       SEMANTIC_NAME(START_TIME);
	F1 end_time         SEMANTIC_NAME(END_TIME);
	F2 start_pos        SEMANTIC_NAME(START_POS);

	F4 start_color      SEMANTIC_NAME(START_COLOR);

	F4 end_color        SEMANTIC_NAME(END_COLOR);

	F2 start_velocity   SEMANTIC_NAME(START_VELOCITY);
	F2 acceleration     SEMANTIC_NAME(ACCELERATION);

	F2 sin_inner_coeff  SEMANTIC_NAME(SIN_INNER_COEFF);
	F2 sin_outer_coeff  SEMANTIC_NAME(SIN_OUTER_COEFF);

	F2 sin_phase_offset SEMANTIC_NAME(SIN_PHASE_OFFSET);
};

#endif

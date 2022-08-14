#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "random.h"

#include "particles_gpu_data_types.h"

#define PARTICLES_MAX_INSTANCES 4096
struct Particles
{
	Max_Length_Array<Particle_Instance, PARTICLES_MAX_INSTANCES> particles;
};

void particles_init(Particles* particles);
void particles_add(Particles* particles, Particle_Instance instance);
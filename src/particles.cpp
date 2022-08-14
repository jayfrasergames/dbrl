#include "particles.h"

#include "stdafx.h"

void particles_init(Particles* particles)
{
	memset(particles, 0, sizeof(particles));
}

void particles_add(Particles* particles, Particle_Instance instance)
{
	particles->particles.append(instance);
}
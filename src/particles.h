#ifndef PARTICLES_H
#define PARTICLES_H

#include "prelude.h"
#include "containers.hpp"
#include "random.h"
#include "jfg_d3d11.h"

#include "particles_gpu_data_types.h"

struct Particles_D3D11
{
	ID3D11Buffer             *constant_buffer;
	ID3D11Buffer             *instance_buffer;
	ID3D11ShaderResourceView *instance_buffer_srv;
	ID3D11ComputeShader      *compute_shader;
};

#define PARTICLES_MAX_INSTANCES 4096
struct Particles
{
	Max_Length_Array<Particle_Instance, PARTICLES_MAX_INSTANCES> particles;

	union {
		Particles_D3D11 d3d11;
	};
};

void particles_init(Particles* particles);
void particles_add(Particles* particles, Particle_Instance instance);

u8 particles_d3d11_init(Particles* context, ID3D11Device* device);
void particles_d3d11_free(Particles* context);
void particles_d3d11_draw(Particles* context,
                          ID3D11DeviceContext* dc,
                          ID3D11UnorderedAccessView* output_uav,
                          v2_u32 output_tile_size,
                          f32 time);

#endif

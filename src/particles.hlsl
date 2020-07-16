#define JFG_HLSL
#include "particles_gpu_data_types.h"

cbuffer particles_constants : register(b0)
{
	Particles_Constant_Buffer constants;
};

StructuredBuffer<Particle_Instance> instances : register(t0);
RWTexture2D<float4>                 output    : register(u0);

[numthreads(PARTICLES_WIDTH, 1, 1)]
void cs_particles(uint tid : SV_DispatchThreadID)
{
	Particle_Instance instance = instances[tid];

	float t = constants.time - instance.start_time;
	float dt = t / (instance.end_time - instance.start_time);
	if (dt < 0.0f || dt > 1.0f) {
		return;
	}

	float4 color = lerp(instance.start_color, instance.end_color, float4(dt, dt, dt, dt));

	float2 pos = instance.start_pos + t * instance.start_velocity;
	pos += t*t * instance.acceleration;
	pos += instance.sin_outer_coeff * sin(instance.sin_phase_offset + instance.sin_inner_coeff * t);

	uint2 output_loc = floor((pos + 0.5f) * constants.tile_size + 0.5f);

	output[output_loc] = color;
}

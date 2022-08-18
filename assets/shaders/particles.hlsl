#define JFG_HLSL
#include "particles_gpu_data_types.h"

cbuffer particles_constants : register(b0)
{
	Particles_Constant_Buffer constants;
};

struct VS_Particles_Output
{
	float4 color : COLOR;
	float4 pos   : SV_Position;
};

VS_Particles_Output vs_particles(Particle_Instance instance)
{
	VS_Particles_Output result;
	result.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
	result.pos = float4(0.0f, 0.0f, 0.0f, 1.0f);

	float t = constants.time - instance.start_time;
	float dt = t / (instance.end_time - instance.start_time);

	if (dt < 0.0f || dt > 1.0f) {
		return result;
	}

	float4 color = lerp(instance.start_color, instance.end_color, float4(dt, dt, dt, dt));

	float2 pos = instance.start_pos + t * instance.start_velocity;
	pos += t*t * instance.acceleration / 2.0f;
	pos += instance.sin_outer_coeff * sin(instance.sin_phase_offset + instance.sin_inner_coeff * t);

	pos += 0.5f;
	pos /= constants.world_size;
	pos *= float2(2.0f, -2.0f);
	pos -= float2(1.0f, -1.0f);

	result.color = color;
	result.pos = float4(pos, 0.0f, 1.0f);

	return result;
}

float4 ps_particles(float4 color : COLOR) : SV_Target0
{
	return color;
}
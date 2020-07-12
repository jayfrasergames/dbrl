#ifndef PARTICLES_H
#define PARTICLES_H

#include "jfg/prelude.h"
#include "jfg/containers.hpp"
#include "jfg/random.h"

#include "particles_gpu_data_types.h"

#ifdef JFG_D3D11_H

#ifndef PARTICLES_DEFINE_GFX
#define PARTICLES_DEFINE_GFX
#endif

struct Particles_D3D11
{
	ID3D11Buffer             *constant_buffer;
	ID3D11Buffer             *instance_buffer;
	ID3D11ShaderResourceView *instance_buffer_srv;
	ID3D11ComputeShader      *compute_shader;
};

#endif

#define PARTICLES_MAX_INSTANCES 4096
struct Particles
{
	Max_Length_Array<Particle_Instance, PARTICLES_MAX_INSTANCES> particles;

#ifdef PARTICLES_DEFINE_GFX
	union {
	#ifdef JFG_D3D11_H
		Particles_D3D11 d3d11;
	#endif
	};
#endif
};

#ifndef JFG_HEADER_ONLY
void particles_init(Particles* particles)
{
	memset(particles, 0, sizeof(particles));
}

void particles_add(Particles* particles, Particle_Instance instance)
{
	particles->particles.append(instance);
}
#endif

#ifdef JFG_D3D11_H

#ifndef JFG_HEADER_ONLY
#include "gen/particles_dxbc_compute_shader.data.h"

u8 particles_d3d11_init(Particles* context, ID3D11Device* device)
{
	HRESULT hr;
	ID3D11ComputeShader *cs = NULL;
	hr = device->CreateComputeShader(PARTICLES_DXBC_CS, sizeof(PARTICLES_DXBC_CS), NULL, &cs);
	if (FAILED(hr)) {
		goto error_init_compute_shader;
	}

	ID3D11Buffer *constant_buffer = NULL;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Particles_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Particles_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_constant_buffer;
	}

	ID3D11Buffer *instance_buffer = NULL;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Particle_Instance) * PARTICLES_MAX_INSTANCES;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(Particle_Instance);

		hr = device->CreateBuffer(&desc, NULL, &instance_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer;
	}

	ID3D11ShaderResourceView *instance_buffer_srv = NULL;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.ElementOffset = 0;
		desc.Buffer.ElementWidth = PARTICLES_MAX_INSTANCES;

		hr = device->CreateShaderResourceView(instance_buffer, &desc, &instance_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer_srv;
	}

	context->d3d11.compute_shader = cs;
	context->d3d11.constant_buffer = constant_buffer;
	context->d3d11.instance_buffer = instance_buffer;
	context->d3d11.instance_buffer_srv = instance_buffer_srv;
	return 1;

	instance_buffer_srv->Release();
error_init_instance_buffer_srv:
	instance_buffer->Release();
error_init_instance_buffer:
	constant_buffer->Release();
error_init_constant_buffer:
	cs->Release();
error_init_compute_shader:
	return 0;
}

void particles_d3d11_free(Particles* context)
{
	context->d3d11.instance_buffer_srv->Release();
	context->d3d11.instance_buffer->Release();
	context->d3d11.constant_buffer->Release();
	context->d3d11.compute_shader->Release();
	memset(&context->d3d11, 0, sizeof(context->d3d11));
}

void particles_d3d11_draw(Particles* context,
                          ID3D11DeviceContext* dc,
                          ID3D11UnorderedAccessView* output_uav,
                          v2_u32 output_tile_size,
                          f32 time)
{
	Particles_Constant_Buffer constants = {};
	constants.tile_size = (v2)output_tile_size;
	constants.time = time;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = dc->Map(context->d3d11.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped.pData, &constants, sizeof(constants));
	dc->Unmap(context->d3d11.constant_buffer, 0);

	for (u32 i = 0; i < context->particles.len; ) {
		if (context->particles[i].end_time < time) {
			context->particles.remove(i);
			continue;
		}
		++i;
	}

	hr = dc->Map(context->d3d11.instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped.pData,
	       context->particles.items,
	       sizeof(context->particles.items[0]) * context->particles.len);
	dc->Unmap(context->d3d11.instance_buffer, 0);

	dc->CSSetConstantBuffers(0, 1, &context->d3d11.constant_buffer);
	dc->CSSetShaderResources(0, 1, &context->d3d11.instance_buffer_srv);
	dc->CSSetUnorderedAccessViews(0, 1, &output_uav, NULL);
	dc->CSSetShader(context->d3d11.compute_shader, NULL, 0);

	dc->Dispatch(context->particles.len, 1, 1);

	ID3D11UnorderedAccessView *null_uav = NULL;
	ID3D11ShaderResourceView *null_srv = NULL;
	dc->CSSetUnorderedAccessViews(0, 1, &null_uav, NULL);
	dc->CSSetShaderResources(0, 1, &null_srv);
}
#endif // JFG_HEADER_ONLY
#endif // JFG_D3D11_H

#endif

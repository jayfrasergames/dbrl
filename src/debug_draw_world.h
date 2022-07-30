#ifndef DEBUG_DRAW_WORLD_H
#define DEBUG_DRAW_WORLD_H

#include "prelude.h"
#include "containers.hpp"

#include "debug_draw_world_gpu_data_types.h"

#define DEBUG_DRAW_MAX_TRIANGLES (4 * 1024 * 1024)
#define DEBUG_DRAW_MAX_LINES     4096
#define DEBUG_DRAW_MAX_STATES    32

#ifdef JFG_D3D11_H

#ifndef INCLUDE_GFX_API
#define INCLUDE_GFX_API
#endif

struct Debug_Draw_World_D3D11
{
	ID3D11Buffer             *triangle_constant_buffer;
	ID3D11Buffer             *triangle_instance_buffer;
	ID3D11ShaderResourceView *triangle_instance_buffer_srv;
	ID3D11VertexShader       *triangle_vertex_shader;
	ID3D11PixelShader        *triangle_pixel_shader;
	ID3D11BlendState         *triangle_blend_state;
	ID3D11Buffer             *line_instance_buffer;
	ID3D11ShaderResourceView *line_instance_buffer_srv;
	ID3D11VertexShader       *line_vertex_shader;
	ID3D11PixelShader        *line_pixel_shader;
};

#endif // JFG_D3D11_H

struct Debug_Draw_World;
extern thread_local Debug_Draw_World *debug_draw_world_context;

struct Debug_Draw_World_State
{
	u32 triangle_len;
	u32 lines_len;
	v4 color;
};

struct Debug_Draw_World
{
	Max_Length_Array<Debug_Draw_World_Triangle, DEBUG_DRAW_MAX_TRIANGLES> triangles;
	Max_Length_Array<Debug_Draw_World_Line, DEBUG_DRAW_MAX_LINES> lines;
	v4 current_color;
	Stack<Debug_Draw_World_State, DEBUG_DRAW_MAX_STATES> states;

#ifdef INCLUDE_GFX_API
	union {
	#ifdef JFG_D3D11_H
		Debug_Draw_World_D3D11 d3d11;
	#endif
	};
#endif

	void set_current() { debug_draw_world_context = this; }
	void reset()
	{
		triangles.reset();
		lines.reset();
		states.reset();
		Debug_Draw_World_State state = {};
		state.triangle_len = 0;
		state.lines_len = 0;
		state.color = v4(1.0f, 0.0f, 0.0f, 1.0f);
		states.push(state);
	}
};

void debug_draw_world_reset();
void debug_draw_world_push_state();
void debug_draw_world_restore_state();
void debug_draw_world_pop_state();
void debug_draw_world_set_color(v4 color);
void debug_draw_world_arrow(v2 start, v2 end);
void debug_draw_world_circle(v2 center, f32 radius);
void debug_draw_world_triangle(v2 a, v2 b, v2 c);
void debug_draw_world_sqaure(v2 center, f32 edge_length);
void debug_draw_world_line(v2 start, v2 end);

#ifndef JFG_HEADER_ONLY
#include "jfg_math.h"
#include "gen/debug_draw_world_dxbc_triangle_vertex_shader.data.h"
#include "gen/debug_draw_world_dxbc_triangle_pixel_shader.data.h"
#include "gen/debug_draw_world_dxbc_line_vertex_shader.data.h"
#include "gen/debug_draw_world_dxbc_line_pixel_shader.data.h"

thread_local Debug_Draw_World *debug_draw_world_context = NULL;

void debug_draw_world_reset()
{
	ASSERT(debug_draw_world_context);
	debug_draw_world_context->reset();
}

void debug_draw_world_push_state()
{
	ASSERT(debug_draw_world_context);
	Debug_Draw_World_State state = {};
	state.triangle_len = debug_draw_world_context->triangles.len;
	state.lines_len = debug_draw_world_context->lines.len;
	state.color = debug_draw_world_context->current_color;
	debug_draw_world_context->states.push(state);
}

void debug_draw_world_restore_state()
{
	ASSERT(debug_draw_world_context);
	Debug_Draw_World_State state = debug_draw_world_context->states.peek();
	debug_draw_world_context->triangles.len = state.triangle_len;
	debug_draw_world_context->lines.len = state.lines_len;
	debug_draw_world_context->current_color = state.color;
}

void debug_draw_world_pop_state()
{
	ASSERT(debug_draw_world_context);
	debug_draw_world_context->states.pop();
	debug_draw_world_restore_state();
}

void debug_draw_world_set_color(v4 color)
{
	ASSERT(debug_draw_world_context);
	debug_draw_world_context->current_color = color;
}

void debug_draw_world_arrow(v2 start, v2 end)
{
	ASSERT(debug_draw_world_context);
	ASSERT((start.x != end.x) || (start.y != end.y));
	v2 dir = end - start;
	f32 angle = atan2f(dir.y, dir.x);
	m2 rot = m2::rotation(angle);

	v2 end_offset = end + 0.5f;
	v2 start_offset = start + 0.5f;

	v2 a = rot * v2(-0.125f,  0.125f) + start_offset;
	v2 b = rot * v2(-0.125f, -0.125f) + start_offset;

	v2 c = rot * v2(-0.25f,  0.125f) + end_offset;
	v2 d = rot * v2(-0.25f, -0.125f) + end_offset;

	v2 e = rot * v2(-0.25f,  0.25f) + end_offset;
	v2 f = rot * v2(-0.25f, -0.25f) + end_offset;
	v2 g = rot * v2(0.125f,   0.0f) + end_offset;

	Debug_Draw_World_Triangle t = {};
	t.color = debug_draw_world_context->current_color;
	t.a = a; t.b = b; t.c = c;
	debug_draw_world_context->triangles.append(t);
	t.a = b; t.b = c; t.c = d;
	debug_draw_world_context->triangles.append(t);
	t.a = e; t.b = f; t.c = g;
	debug_draw_world_context->triangles.append(t);
}

void debug_draw_world_circle(v2 center, f32 radius)
{
	ASSERT(debug_draw_world_context);

	u32 num_points = 4;

	center = center + 0.5f;
	v2 v = v2(radius, 0.0f);

	m2 m = m2::rotation(2.0f * PI_F32 / (f32)(4 * num_points));

	Debug_Draw_World_Triangle t = {};
	t.color = debug_draw_world_context->current_color;
	for (u32 i = 0; i < 4 * num_points; ++i) {
		v2 w = m * v;
		t.a = center;
		t.b = center + v;
		t.c = center + w;
		debug_draw_world_context->triangles.append(t);
		v = w;
	}
}

void debug_draw_world_triangle(v2 a, v2 b, v2 c)
{
	ASSERT(debug_draw_world_context);

	Debug_Draw_World_Triangle t = {};
	t.a = a + 0.5f;
	t.b = b + 0.5f;
	t.c = c + 0.5f;
	t.color = debug_draw_world_context->current_color;
	debug_draw_world_context->triangles.append(t);
}

void debug_draw_world_sqaure(v2 center, f32 edge_length)
{
	ASSERT(debug_draw_world_context);

	f32 k = edge_length / 2.0f;
	Debug_Draw_World_Triangle t = {};
	t.a = center + 0.5f + v2(-k, -k);
	t.b = center + 0.5f + v2( k, -k);
	t.c = center + 0.5f + v2(-k,  k);
	t.color = debug_draw_world_context->current_color;
	debug_draw_world_context->triangles.append(t);

	t.a = center + 0.5f + v2(-k,  k);
	t.b = center + 0.5f + v2( k, -k);
	t.c = center + 0.5f + v2( k,  k);
	debug_draw_world_context->triangles.append(t);
}

void debug_draw_world_line(v2 start, v2 end)
{
	ASSERT(debug_draw_world_context);

	Debug_Draw_World_Line l = {};
	l.start = start + 0.5f;
	l.end = end + 0.5f;
	l.color = debug_draw_world_context->current_color;
	debug_draw_world_context->lines.append(l);
}
#endif // JFG_HEADER_ONLY

#ifdef JFG_D3D11_H

u8 debug_draw_world_d3d11_init(Debug_Draw_World* context, ID3D11Device* device);
void debug_draw_world_d3d11_free(Debug_Draw_World* context);
void debug_draw_world_d3d11_draw(Debug_Draw_World*       context,
                                 ID3D11DeviceContext*    dc,
                                 ID3D11RenderTargetView* output_rtv,
                                 v2                      world_center,
                                 v2                      zoom);

#ifndef JFG_HEADER_ONLY

u8 debug_draw_world_d3d11_init(Debug_Draw_World* context, ID3D11Device* device)
{
	HRESULT hr;
	hr = device->CreateVertexShader(DDW_TRIANGLE_DXBC_VS,
	                                ARRAY_SIZE(DDW_TRIANGLE_DXBC_VS),
	                                NULL,
	                                &context->d3d11.triangle_vertex_shader);
	if (FAILED(hr)) {
		goto error_init_triangle_vertex_shader;
	}

	hr = device->CreatePixelShader(DDW_TRIANGLE_DXBC_PS,
	                               ARRAY_SIZE(DDW_TRIANGLE_DXBC_PS),
	                               NULL,
	                               &context->d3d11.triangle_pixel_shader);
	if (FAILED(hr)) {
		goto error_init_triangle_pixel_shader;
	}

	// create triangle instance buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(context->triangles.items);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(context->triangles.items[0]);

		hr = device->CreateBuffer(&desc, NULL, &context->d3d11.triangle_instance_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_triangle_instance_buffer;
	}

	// create triangle instance buffer srv
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements  = ARRAY_SIZE(context->triangles.items);

		hr = device->CreateShaderResourceView(context->d3d11.triangle_instance_buffer,
		                                      &desc,
		                                      &context->d3d11.triangle_instance_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_triangle_instance_buffer_srv;
	}

	// create constant buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Debug_Draw_World_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Debug_Draw_World_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &context->d3d11.triangle_constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_triangle_constant_buffer;
	}

	// create blend state
	{
		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;
		desc.RenderTarget[0].BlendEnable = TRUE;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = device->CreateBlendState(&desc, &context->d3d11.triangle_blend_state);
	}
	if (FAILED(hr)) {
		goto error_init_blend_state;
	}

	hr = device->CreateVertexShader(DDW_LINE_DXBC_VS,
	                                ARRAY_SIZE(DDW_LINE_DXBC_VS),
	                                NULL,
	                                &context->d3d11.line_vertex_shader);
	if (FAILED(hr)) {
		goto error_init_line_vertex_shader;
	}

	hr = device->CreatePixelShader(DDW_LINE_DXBC_PS,
	                               ARRAY_SIZE(DDW_LINE_DXBC_PS),
	                               NULL,
	                               &context->d3d11.line_pixel_shader);
	if (FAILED(hr)) {
		goto error_init_line_pixel_shader;
	}

	// create line instance buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(context->lines.items);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(context->lines.items[0]);

		hr = device->CreateBuffer(&desc, NULL, &context->d3d11.line_instance_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_line_instance_buffer;
	}

	// create line instance buffer srv
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements  = ARRAY_SIZE(context->lines.items);

		hr = device->CreateShaderResourceView(context->d3d11.line_instance_buffer,
		                                      &desc,
		                                      &context->d3d11.line_instance_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_line_instance_buffer_srv;
	}

	return 1;

	context->d3d11.line_instance_buffer_srv->Release();
error_init_line_instance_buffer_srv:
	context->d3d11.line_instance_buffer->Release();
error_init_line_instance_buffer:
	context->d3d11.line_pixel_shader->Release();
error_init_line_pixel_shader:
	context->d3d11.line_vertex_shader->Release();
error_init_line_vertex_shader:
	context->d3d11.triangle_blend_state->Release();
error_init_blend_state:
	context->d3d11.triangle_constant_buffer->Release();
error_init_triangle_constant_buffer:
	context->d3d11.triangle_instance_buffer_srv->Release();
error_init_triangle_instance_buffer_srv:
	context->d3d11.triangle_instance_buffer->Release();
error_init_triangle_instance_buffer:
	context->d3d11.triangle_pixel_shader->Release();
error_init_triangle_pixel_shader:
	context->d3d11.triangle_vertex_shader->Release();
error_init_triangle_vertex_shader:
	return 0;
}

void debug_draw_world_d3d11_free(Debug_Draw_World* context)
{
	context->d3d11.line_instance_buffer_srv->Release();
	context->d3d11.line_instance_buffer->Release();
	context->d3d11.line_pixel_shader->Release();
	context->d3d11.line_vertex_shader->Release();
	context->d3d11.triangle_blend_state->Release();
	context->d3d11.triangle_constant_buffer->Release();
	context->d3d11.triangle_instance_buffer_srv->Release();
	context->d3d11.triangle_instance_buffer->Release();
	context->d3d11.triangle_pixel_shader->Release();
	context->d3d11.triangle_vertex_shader->Release();
}

void debug_draw_world_d3d11_draw(Debug_Draw_World*       context,
                                 ID3D11DeviceContext*    dc,
                                 ID3D11RenderTargetView* output_rtv,
                                 v2                      world_center,
                                 v2                      zoom)
{
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE mapped;
	hr = dc->Map(context->d3d11.triangle_instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped.pData,
	       context->triangles.items,
	       context->triangles.len * sizeof(context->triangles.items[0]));
	dc->Unmap(context->d3d11.triangle_instance_buffer, 0);

	hr = dc->Map(context->d3d11.line_instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped.pData,
	       context->lines.items,
	       context->lines.len * sizeof(context->lines.items[0]));
	dc->Unmap(context->d3d11.line_instance_buffer, 0);

	Debug_Draw_World_Constant_Buffer constant_buffer = {};
	constant_buffer.center = -world_center;
	constant_buffer.zoom = zoom;
	hr = dc->Map(context->d3d11.triangle_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped.pData, &constant_buffer, sizeof(constant_buffer));
	dc->Unmap(context->d3d11.triangle_constant_buffer, 0);

	dc->IASetInputLayout(NULL);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dc->VSSetConstantBuffers(0, 1, &context->d3d11.triangle_constant_buffer);
	dc->VSSetShaderResources(0, 1, &context->d3d11.triangle_instance_buffer_srv);
	dc->VSSetShader(context->d3d11.triangle_vertex_shader, NULL, 0);

	dc->PSSetShader(context->d3d11.triangle_pixel_shader, NULL, 0);

	ID3D11RenderTargetView *rtvs[] = { output_rtv, NULL };
	dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, NULL);
	dc->OMSetBlendState(context->d3d11.triangle_blend_state, NULL, 0xFFFFFFFF);

	dc->DrawInstanced(3, context->triangles.len, 0, 0);

	dc->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);

	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	dc->VSSetShaderResources(0, 1, &context->d3d11.line_instance_buffer_srv);
	dc->VSSetShader(context->d3d11.line_vertex_shader, NULL, 0);

	dc->PSSetShader(context->d3d11.line_pixel_shader, NULL, 0);

	dc->DrawInstanced(2, context->lines.len, 0, 0);
}

#endif // JFG_HEADER_ONLY
#endif // JFG_D3D11_H


#endif

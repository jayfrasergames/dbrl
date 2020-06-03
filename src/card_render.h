#ifndef CARD_RENDER_H
#define CARD_RENDER_H

#include "jfg/prelude.h"

#ifdef JFG_D3D11_H

#ifndef JFG_INCLUDE_GFX
#define JFG_INCLUDE_GFX
#endif

struct Card_Render_D3D11
{
	ID3D11VertexShader       *cards_vs;
	ID3D11PixelShader        *cards_ps;
	ID3D11Texture2D          *cards_tex;
	ID3D11ShaderResourceView *cards_srv;
	ID3D11Buffer             *instances;
	ID3D11ShaderResourceView *instances_srv;
	ID3D11Buffer             *cards_cb;
};
#endif

#include "card_render_gpu_data_types.h"

#define CARD_RENDER_MAX_INSTANCES 1024

struct Card_Render
{
	u32                  num_instances;
	Card_Render_Instance instances[CARD_RENDER_MAX_INSTANCES];
#ifdef JFG_INCLUDE_GFX
	union {
	#ifdef JFG_D3D11_H
		Card_Render_D3D11 d3d11;
	#endif
	};
#endif
};

void card_render_reset(Card_Render* render);
void card_render_add_instance(Card_Render* render, Card_Render_Instance instance);
void card_render_z_sort(Card_Render* render);
u32 card_render_get_card_id_from_mouse_pos(Card_Render* render, v2 mouse_pos);

#ifdef JFG_DEBUG_LINE_DRAW_H
void card_render_draw_debug_lines(Card_Render* render, Debug_Line* debug_line, u32 selected_id);
#endif

#ifndef JFG_HEADER_ONLY
void card_render_reset(Card_Render* render)
{
	render->num_instances = 0;
}

void card_render_add_instance(Card_Render* render, Card_Render_Instance instance)
{
	ASSERT(render->num_instances < CARD_RENDER_MAX_INSTANCES);
	render->instances[render->num_instances++] = instance;
}

void card_render_z_sort(Card_Render* render)
{
	// XXX -- insertion sort because why not
	u32 num_instances = render->num_instances;
	Card_Render_Instance *instances = render->instances;
	for (u32 i = 1; i < num_instances; ++i) {
		Card_Render_Instance tmp = instances[i];
		u32 j = i;
		while (j && instances[j - 1].z_offset > tmp.z_offset) {
			instances[j] = instances[j - 1];
			--j;
		}
		instances[j] = tmp;
	}
}

u32 card_render_get_card_id_from_mouse_pos(Card_Render* render, v2 mouse_pos)
{
	// TODO -- we will need to keep track of best z to get the card on top
	f32 best_z = -1.0f;
	u32 best_id = 0;

	//constant_buffer.card_size = { 48.0f, 80.0f };
	// XXX -- lots of magic constants here...
	v2 card_size = { 0.5f * 0.4f * 48.0f / 80.0f, 0.5f * 0.4f * 1.0f };

	u32 num_cards = render->num_instances;
	for (u32 i = 0; i < num_cards; ++i) {
		Card_Render_Instance *instance = &render->instances[i];

		v2 top_left = {};
		v2 top_right = {};
		v2 bottom_left = {};
		v2 bottom_right = {};

		v2 center = instance->screen_pos;
		v2 m = { mouse_pos.x - center.x, mouse_pos.y - center.y };

		f32 c = cosf(instance->screen_rotation);
		f32 s = sinf(instance->screen_rotation);

		m.x = m.x * c + m.y * s;
		m.y = - m.x * s + m.y * c;

		v2 size = card_size;
		size.w *= cosf(instance->horizontal_rotation);
		size.h *= cosf(instance->vertical_rotation);

		if (fabsf(m.x) < size.w && fabsf(m.y) < size.y && instance->z_offset > best_z) {
			best_id = instance->card_id;
			best_z = instance->z_offset;
		}
	}
	return best_id;
}

#ifdef JFG_DEBUG_LINE_DRAW_H
void card_render_draw_debug_lines(Card_Render* render, Debug_Line* debug_line, u32 selected_id)
{
	Debug_Line_Instance line = {};

	//constant_buffer.card_size = { 48.0f, 80.0f };
	// XXX -- lots of magic constants here...
	v2 card_size = { 0.5f * 0.4f * 48.0f / 80.0f, 0.5f * 0.4f * 1.0f };

	u32 num_cards = render->num_instances;
	for (u32 i = 0; i < num_cards; ++i) {
		Card_Render_Instance *instance = &render->instances[i];
		if (instance->card_id == selected_id) {
			line.color = { 0.0f, 1.0f, 0.0f, 1.0f };
		} else {
			line.color = { 1.0f, 0.0f, 0.0f, 1.0f };
		}

		v2 top_left = {};
		v2 top_right = {};
		v2 bottom_left = {};
		v2 bottom_right = {};

		v2 center = instance->screen_pos;
		v2 size = card_size;
		size.w *= cosf(instance->horizontal_rotation);
		size.h *= cosf(instance->vertical_rotation);

		f32 c = cosf(instance->screen_rotation);
		f32 s = sinf(instance->screen_rotation);

		top_right.x = center.x + size.w * c - size.h * s;
		top_right.y = center.y + size.w * s + size.h * c;

		top_left.x = center.x - size.w * c - size.h * s;
		top_left.y = center.y - size.w * s + size.h * c;

		bottom_left.x = center.x - size.w * c + size.h * s;
		bottom_left.y = center.y - size.w * s - size.h * c;

		bottom_right.x = center.x + size.w * c + size.h * s;
		bottom_right.y = center.y + size.w * s - size.h * c;

		line.start = top_left;
		line.end = top_right;
		debug_line_add_instance(debug_line, line);
		line.start = top_right;
		line.end = bottom_right;
		debug_line_add_instance(debug_line, line);
		line.start = bottom_right;
		line.end = bottom_left;
		debug_line_add_instance(debug_line, line);
		line.start = bottom_left;
		line.end = top_left;
		debug_line_add_instance(debug_line, line);
	}
}
#endif // JFG_DEBUG_LINE_DRAW_H
#endif

// ==============================================================================
// D3D11 stuff

#ifdef JFG_D3D11_H
u8 card_render_d3d11_init(Card_Render* card_render, ID3D11Device* device);
void card_render_d3d11_free(Card_Render* card_render);
void card_render_d3d11_draw(Card_Render*            render,
                            ID3D11DeviceContext*    dc,
                            v2_u32                  screen_size,
                            ID3D11RenderTargetView* output_rtv);

#ifndef JFG_HEADER_ONLY
#include "gen/card_render_dxbc_vertex_shader.data.h"
#include "gen/card_render_dxbc_pixel_shader.data.h"
// XXX - include card data
#include "gen/cards.data.h"

u8 card_render_d3d11_init(Card_Render* card_render, ID3D11Device* device)
{
	HRESULT hr;
	hr = device->CreateVertexShader(CARD_RENDER_DXBC_VS,
	                                ARRAY_SIZE(CARD_RENDER_DXBC_VS),
	                                NULL,
	                                &card_render->d3d11.cards_vs);
	if (FAILED(hr)) {
		goto error_init_cards_vs;
	}

	hr = device->CreatePixelShader(CARD_RENDER_DXBC_PS,
	                               ARRAY_SIZE(CARD_RENDER_DXBC_PS),
	                               NULL,
	                               &card_render->d3d11.cards_ps);
	if (FAILED(hr)) {
		goto error_init_cards_ps;
	}

	// create cards_tex
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = CARD_IMAGE_SIZE.w;
		desc.Height = CARD_IMAGE_SIZE.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = CARD_IMAGE_DATA;
		data.SysMemPitch = sizeof(CARD_IMAGE_DATA[0]) * CARD_IMAGE_SIZE.w;
		data.SysMemSlicePitch = sizeof(CARD_IMAGE_DATA);

		hr = device->CreateTexture2D(&desc, &data, &card_render->d3d11.cards_tex);
	}
	if (FAILED(hr)) {
		goto error_init_cards_tex;
	}

	hr = device->CreateShaderResourceView(card_render->d3d11.cards_tex,
	                                      NULL,
	                                      &card_render->d3d11.cards_srv);
	if (FAILED(hr)) {
		goto error_init_cards_srv;
	}

	// create instance buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(card_render->instances);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(card_render->instances[0]);

		hr = device->CreateBuffer(&desc, NULL, &card_render->d3d11.instances);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer;
	}

	// create instance buffer srv
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.ElementOffset = 0;
		desc.Buffer.ElementWidth = CARD_RENDER_MAX_INSTANCES;

		hr = device->CreateShaderResourceView(card_render->d3d11.instances,
		                                      &desc,
		                                      &card_render->d3d11.instances_srv);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer_srv;
	}

	// create constant buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Card_Render_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Card_Render_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &card_render->d3d11.cards_cb);
	}
	if (FAILED(hr)) {
		goto error_init_constant_buffer;
	}

	return 1;

	card_render->d3d11.cards_cb->Release();
error_init_constant_buffer:
	card_render->d3d11.instances_srv->Release();
error_init_instance_buffer_srv:
	card_render->d3d11.instances->Release();
error_init_instance_buffer:
	card_render->d3d11.cards_srv->Release();
error_init_cards_srv:
	card_render->d3d11.cards_tex->Release();
error_init_cards_tex:
	card_render->d3d11.cards_ps->Release();
error_init_cards_ps:
	card_render->d3d11.cards_vs->Release();
error_init_cards_vs:
	return 0;
}

void card_render_d3d11_free(Card_Render* card_render)
{
	card_render->d3d11.cards_cb->Release();
	card_render->d3d11.instances_srv->Release();
	card_render->d3d11.instances->Release();
	card_render->d3d11.cards_srv->Release();
	card_render->d3d11.cards_tex->Release();
	card_render->d3d11.cards_ps->Release();
	card_render->d3d11.cards_vs->Release();
}

void card_render_d3d11_draw(Card_Render*            render,
                            ID3D11DeviceContext*    dc,
                            v2_u32                  screen_size,
                            ID3D11RenderTargetView* output_rtv)
{
	Card_Render_Constant_Buffer constant_buffer = {};
	constant_buffer.card_size = { 48.0f, 80.0f };
	constant_buffer.screen_size = { (f32)screen_size.w, (f32)screen_size.h };
	constant_buffer.tex_size = { (f32)CARD_IMAGE_SIZE.w, (f32)CARD_IMAGE_SIZE.h };
	constant_buffer.zoom = 0.4f;

	D3D11_MAPPED_SUBRESOURCE mapped_buffer = {};

	HRESULT hr;
	hr = dc->Map(render->d3d11.cards_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &constant_buffer, sizeof(constant_buffer));
	dc->Unmap(render->d3d11.cards_cb, 0);

	hr = dc->Map(render->d3d11.instances, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData,
	       render->instances,
	       render->num_instances * sizeof(render->instances[0]));
	dc->Unmap(render->d3d11.instances, 0);

	dc->IASetInputLayout(NULL);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dc->VSSetShader(render->d3d11.cards_vs, NULL, 0);
	dc->VSSetConstantBuffers(0, 1, &render->d3d11.cards_cb);
	dc->VSSetShaderResources(0, 1, &render->d3d11.instances_srv);

	dc->PSSetShader(render->d3d11.cards_ps, NULL, 0);
	dc->PSSetConstantBuffers(0, 1, &render->d3d11.cards_cb);
	dc->PSSetShaderResources(0, 1, &render->d3d11.cards_srv);

	dc->OMSetRenderTargets(1, &output_rtv, NULL);

	dc->DrawInstanced(6, render->num_instances, 0, 0);
}

#endif // JFG_HEADER_ONLY
#endif // JFG_D3D11_H

#endif

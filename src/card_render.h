#ifndef CARD_RENDER_H
#define CARD_RENDER_H

#include "stdafx.h"
#include "jfg_math.h"

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
	ID3D11RasterizerState    *rasterizer_state;
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

// ==============================================================================
// D3D11 stuff

#ifdef JFG_D3D11_H
u8 card_render_d3d11_init(Card_Render* card_render, ID3D11Device* device);
void card_render_d3d11_free(Card_Render* card_render);
void card_render_d3d11_draw(Card_Render*            render,
                            ID3D11DeviceContext*    dc,
                            v2_u32                  screen_size,
                            ID3D11RenderTargetView* output_rtv);
#endif

#endif

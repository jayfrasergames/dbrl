#ifndef DBRL_H
#define DBRL_H

#include "jfg/prelude.h"
#include "jfg/mem.h"
#include "jfg/input.h"

#include "jfg/jfg_d3d11.h"

struct Game;

#define GAME_FUNCTIONS \
	/* start list */ \
	GAME_FUNCTION(Memory_Spec, get_game_size) \
	GAME_FUNCTION(void, game_init, Game *game) \
	GAME_FUNCTION(u8, game_d3d11_init, Game *game, ID3D11Device *device, v2_u32 screen_size) \
	GAME_FUNCTION(void, game_d3d11_free, Game *game) \
	GAME_FUNCTION(u8, game_d3d11_set_screen_size, Game *game, ID3D11Device* device, v2_u32 screen_size) \
	GAME_FUNCTION(void, process_frame, Game *game, Input *input, v2_u32 screen_size) \
	GAME_FUNCTION(void, render_d3d11, Game *game, ID3D11DeviceContext *dc, ID3D11RenderTargetView* output_rtv) \
	/* end list */


#ifdef DEBUG
	#ifdef LIBRARY
		#define GAME_FUNCTION(return_type, name, ...) LIBRARY_EXPORT return_type name(__VA_ARGS__);
		GAME_FUNCTIONS
		#undef GAME_FUNCTION
	#endif
#else
	#define GAME_FUNCTION(return_type, name, ...) return_type name(__VA_ARGS__);
	GAME_FUNCTIONS
	#undef GAME_FUNCTION
#endif

#endif

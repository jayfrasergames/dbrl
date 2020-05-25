#ifndef DBRL_H
#define DBRL_H

#include "jfg/prelude.h"
#include "jfg/mem.h"
#include "jfg/input.h"

#include "jfg/jfg_d3d11.h"

struct Program;

#define GAME_FUNCTIONS \
	/* start list */ \
	GAME_FUNCTION(Memory_Spec, get_program_size) \
	GAME_FUNCTION(void, program_init, Program *program) \
	GAME_FUNCTION(u8, program_d3d11_init, Program *program, ID3D11Device *device, v2_u32 screen_size) \
	GAME_FUNCTION(void, program_d3d11_free, Program *program) \
	GAME_FUNCTION(u8, program_d3d11_set_screen_size, Program *program, ID3D11Device* device, v2_u32 screen_size) \
	GAME_FUNCTION(void, process_frame, Program *program, Input *input, v2_u32 screen_size) \
	GAME_FUNCTION(void, render_d3d11, Program *program, ID3D11DeviceContext *dc, ID3D11RenderTargetView* output_rtv) \
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

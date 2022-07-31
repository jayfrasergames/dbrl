#ifndef DBRL_H
#define DBRL_H

#include "prelude.h"
#include "mem.h"
#include "input.h"
#include "draw.h"

#include "jfg_d3d11.h"
#include "jfg_dsound.h"

#include "platform_functions.h"

struct Program;

#define GAME_FUNCTIONS \
	/* start list */ \
	GAME_FUNCTION(Memory_Spec, get_program_size) \
	GAME_FUNCTION(void, program_init, Program *program, Draw* draw, Platform_Functions platform_functions) \
	GAME_FUNCTION(u8, program_dsound_init, Program *program, IDirectSound *dsound) \
	GAME_FUNCTION(void, program_dsound_free, Program *program) \
	GAME_FUNCTION(void, program_dsound_play, Program *program) \
	GAME_FUNCTION(void, process_frame, Program *program, Input *input, v2_u32 screen_size) \
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

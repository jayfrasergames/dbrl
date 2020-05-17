#ifndef DBRL_H
#define DBRL_H

#include "jfg/prelude.h"
#include "jfg/mem.h"
#include "jfg/input.h"

struct Game;

#define GAME_FUNCTIONS \
	/* start list */ \
	GAME_FUNCTION(Memory_Spec, get_game_size) \
	GAME_FUNCTION(void, init_game, Game *game) \
	GAME_FUNCTION(void, process_frame, Game *game, Input *input, v2_u32 screen_size) \
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

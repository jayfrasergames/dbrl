#pragma once

#include "prelude.h"

#include "log.h"
#include "game.h"

typedef void (*Build_Level_Function)(Game* game, Log* log);

#define LEVEL_GEN_FUNCS \
	FUNC(default) \
	FUNC(anim_test) \
	FUNC(slime_test) \
	FUNC(lich_test) \
	FUNC(field_of_vision_test) \
	FUNC(spider_room)

// declare functions
#define FUNC(name) void build_level_##name(Game* game, Log* log);
	LEVEL_GEN_FUNCS
#undef FUNC

enum Build_Level_Func_ID
{
#define FUNC(name) BUILD_LEVEL_FUNC_##name,
	LEVEL_GEN_FUNCS
#undef FUNC

	NUM_LEVEL_GEN_FUNCS
};

struct Build_Level_Func_Metadata
{
	const char          *name;
	Build_Level_Function func;
};

extern Build_Level_Func_Metadata BUILD_LEVEL_FUNCS[NUM_LEVEL_GEN_FUNCS];
#include "dbrl.h"

struct Game
{
};

Memory_Spec get_game_size()
{
	Memory_Spec result = {};
	result.alignment = alignof(Game);
	result.size = sizeof(Game);
	return result;
}

void init_game(Game* game)
{
}

void process_frame(Game* game, Input* input, v2_u32 screen_size)
{
}

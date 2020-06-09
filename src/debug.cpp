
enum Debug_Section
{
	DEBUG_SECTION_DO_MOVE,

	NUM_DEBUG_SECTIONS,
};

void debug_pause()
{
	mutex_get(DEBUG_DRAW_STATE);

	Debug_Draw *tmp = program->debug_draw.front_buffer;
	program->debug_draw.front_buffer = program->debug_draw.back_buffer;
	program->debug_draw.back_buffer = tmp;

	mutex_release(DEBUG_DRAW_STATE);

	mutex_get(PROGRAM_STATE);

	program->state = PROGRAM_STATE_DEBUG_PAUSE;

	mutex_release(PROGRAM_STATE);
}

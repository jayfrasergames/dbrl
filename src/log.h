#pragma once

#include "prelude.h"
#include "jfg_error.h"
#include "types.h"

enum Log_Scroll
{
	LOG_SCROLL_TOP,
	LOG_SCROLL_BOTTOM,
	LOG_SCROLL_UP_ONE,
	LOG_SCROLL_DOWN_ONE,
};

enum Log_Scroll_State
{
	LOG_SCROLL_STATE_TOP,
	LOG_SCROLL_STATE_BOTTOM,
	LOG_SCROLL_STATE_MIDDLE,
};

struct Log
{
	char            *buffer;
	size_t           buffer_size;
	u32              start_pos;
	u32              end_pos;
	u32              read_pos;
	v2_u32           grid_size;
	Log_Scroll_State scroll_state;
};

void init(Log* l, char* buffer, size_t buffer_size);
void reset(Log* l);
void log(Log* l, const char* str);
void logf(Log* l, const char* format_string, ...);
void render(Log* l, Render* render);
void scroll(Log* l, Log_Scroll scroll);
JFG_Error write_to_file(Log* log, const char* filename);
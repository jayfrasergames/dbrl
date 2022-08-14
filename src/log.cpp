#include "log.h"

#include "stdafx.h"

#include "render.h"

const char ESCAPE = 0x1B;

void init(Log* l, char* buffer, size_t buffer_size)
{
	memset(l, 0, sizeof(*l));
	l->buffer = buffer;
	l->buffer_size = buffer_size;
	reset(l);
}

void reset(Log* l)
{
	l->start_pos = 0;
	l->end_pos = 0;
	l->read_pos = 0;
	// XXX - is this necessary?
	l->buffer[0] = 0;
	l->scroll_state = LOG_SCROLL_STATE_BOTTOM;
}

void log(Log* l, const char* str)
{
	size_t buffer_size = l->buffer_size;
	u32 buffer_pos = l->end_pos;
	for (const char *p = str; *p; ++p) {
		l->buffer[buffer_pos % buffer_size] = *p;
		++buffer_pos;
	}
	l->buffer[buffer_pos % buffer_size] = '\n';
	l->end_pos = ++buffer_pos;

	if (l->buffer[buffer_pos % buffer_size]) {
		ASSERT(l->end_pos > l->start_pos);
		while (l->buffer[buffer_pos % buffer_size] != '\n') {
			l->buffer[buffer_pos % buffer_size] = 0;
			++buffer_pos;
		}
		l->buffer[buffer_pos % buffer_size] = 0;
		l->start_pos = ++buffer_pos - buffer_size;
	}
	ASSERT(l->end_pos >= l->start_pos);
	if (l->read_pos < l->start_pos) {
		l->read_pos = l->start_pos;
	}
	if (l->scroll_state == LOG_SCROLL_STATE_BOTTOM) {
		scroll(l, LOG_SCROLL_BOTTOM);
	}
}

void logf(Log* l, const char* format_string, ...)
{
	char print_buffer[4096];
	va_list args;
	va_start(args, format_string);
	vsnprintf(print_buffer, ARRAY_SIZE(print_buffer), format_string, args);
	va_end(args);

	log(l, print_buffer);
}

void render(Log* l, Render* render)
{
	auto r = &render->render_job_buffer;

	v2 glyph_size = v2(9.0f, 16.0f);

	v2_u32 grid_size = l->grid_size;

	v2 console_offset = v2(5.0f, 2.0f);
	v2 console_border = v2 (1.0f, 1.0f);
	v2 console_size   = (v2)grid_size;

	v2 top_left = glyph_size * console_offset;
	v2 bottom_right = top_left + glyph_size * (console_size + 2.0f * console_border);
	v2 top_right = v2(bottom_right.x, top_left.y);
	v2 bottom_left = v2(top_left.x, bottom_right.y);

	Triangle_Instance t = {};
	t.color = v4(0.0f, 0.0f, 0.0f, 0.5f);
	t.a = top_left;
	t.b = top_right;
	t.c = bottom_left;
	push_triangle(r, t);
	t.a = bottom_right;
	push_triangle(r, t);

	Sprite_Constants sprite_constants = {};
	sprite_constants.tile_input_size = glyph_size;
	sprite_constants.tile_output_size = glyph_size;
	begin_sprites(r, SOURCE_TEXTURE_CODEPAGE437, sprite_constants);

	Sprite_Instance instance = {};
	instance.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
	v2_u32 cursor_pos = v2_u32(0, 0);
	v2 console_top_left = console_offset + console_border;

	size_t buffer_size = l->buffer_size;
	for (u32 pos = l->read_pos % buffer_size; l->buffer[pos]; pos = (pos + 1) % buffer_size) {
		u8 c = l->buffer[pos];
		switch (c) {
		case '\n':
		case '\r':
			++cursor_pos.y;
			cursor_pos.x = 0;
			break;
		case '\t':
			cursor_pos.x += 8;
			cursor_pos.x &= ~7;
			break;
		default:
			instance.glyph_coords = v2(c % 32, c / 32);
			instance.output_coords = (v2)cursor_pos + console_top_left;
			push_sprite(r, instance);
			++cursor_pos.x;
			break;
		}
		if (cursor_pos.x >= grid_size.w) {
			++cursor_pos.y;
			cursor_pos.x = 0;
		}
		if (cursor_pos.y >= grid_size.h) {
			break;
		}
	}
}

static u32 get_next_line_start(Log* l, u32 pos)
{
	u32 width = l->grid_size.w;
	u32 end_pos = l->end_pos;
	if (pos == end_pos) {
		return pos;
	}
	u32 col_pos = 0;
	size_t buffer_size = l->buffer_size;
	for ( ; pos < end_pos; ++pos) {
		u8 c = l->buffer[pos % buffer_size];
		switch (c) {
		case '\n':
		case '\r':
			return pos + 1;
		case '\t':
			if (col_pos >= width) {
				return pos;
			}
			col_pos += 8;
			col_pos &= ~7;
			break;
		default:
			if (col_pos >= width) {
				return pos;
			}
			++col_pos;
			break;
		}
	}
	return pos;
}

static u32 get_prev_line_start(Log* l, u32 pos)
{
	u32 start_pos = l->start_pos;
	if (pos == start_pos) {
		return start_pos;
	}
	u32 prev_newline_pos = pos - 1;
	size_t buffer_size = l->buffer_size;
	while (1) {
		char c = l->buffer[(prev_newline_pos - 1) % buffer_size];
		if (c == '\n' || c == 0) {
			break;
		}
		--prev_newline_pos;
	}
	u32 result = prev_newline_pos;
	u32 cur_pos = result;
	while (cur_pos < pos) {
		result = cur_pos;
		cur_pos = get_next_line_start(l, cur_pos);
	}
	return result;
}

void scroll(Log* l, Log_Scroll scroll)
{
	switch (scroll) {
	case LOG_SCROLL_TOP:
		l->read_pos = l->start_pos;
		l->scroll_state = LOG_SCROLL_STATE_TOP;
		break;
	case LOG_SCROLL_UP_ONE:
		l->read_pos = get_prev_line_start(l, l->read_pos);
		l->scroll_state = LOG_SCROLL_STATE_MIDDLE;
		break;
	case LOG_SCROLL_DOWN_ONE:
		l->read_pos = get_next_line_start(l, l->read_pos);
		l->scroll_state = LOG_SCROLL_STATE_MIDDLE;
		break;
	case LOG_SCROLL_BOTTOM: {
		u32 pos = l->end_pos;
		for (u32 i = 0; i < l->grid_size.h; ++i) {
			pos = get_prev_line_start(l, pos);
		}
		l->read_pos = pos;
		l->scroll_state = LOG_SCROLL_STATE_BOTTOM;
		break;
	}
	}
}

JFG_Error write_to_file(Log* log, const char* filename)
{
	u32 start_pos = log->start_pos;
	u32 end_pos = log->end_pos;
	size_t buffer_size = log->buffer_size;
	size_t size = log->end_pos - log->start_pos;

	void *start = &log->buffer[start_pos % buffer_size];
	void *end = &log->buffer[end_pos % buffer_size];

	if (start <= end) {
		return try_write_file(filename, start, size);
	}

	// XXX -- yuck
	void *buffer = malloc(size);
	size_t first_chunk_size = buffer_size - (start_pos % buffer_size);
	size_t second_chunk_size = end_pos % buffer_size;
	ASSERT(size == first_chunk_size + second_chunk_size);
	memcpy(buffer, start, first_chunk_size);
	memcpy((void*)((uptr)buffer + first_chunk_size), log->buffer, second_chunk_size);

	auto error = try_write_file(filename, buffer, size);
	free(buffer);
	return error;
}
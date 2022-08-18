#include "imgui.h"

#include "stdafx.h"

#include "jfg_math.h"
#include "codepage_437.h"

void imgui_begin(IMGUI_Context* context, Input* input)
{
	context->text_pos = { 0.0f, 0.0f };
	context->text_color = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->text_index = 0;
	context->tree_indent_level = 0;
	context->input = input;
}

void imgui_set_text_cursor(IMGUI_Context* context, v4 color, v2 pos)
{
	context->text_pos = pos;
	context->text_color = color;
}

void imgui_text(IMGUI_Context* context, char* format_string, ...)
{
	char buffer[4096];
	va_list args;
	va_start(args, format_string);
	vsnprintf(buffer, ARRAY_SIZE(buffer), format_string, args);
	va_end(args);

	v2 pos = context->text_pos;
	v4 color = context->text_color;
	u32 index = context->text_index;
	IMGUI_VS_Text_Instance *cur_char = &context->text_buffer[index];
	for (u8 *p = (u8*)buffer; *p; ++p) {
		if (index == IMGUI_MAX_TEXT_CHARACTERS) {
			break;
		}
		u8 c = *p;
		switch (c) {
		case '\n':
			pos.x = 0.0f;
			++pos.y;
			break;
		case '\t':
			// XXX - ignore proper tab behaviour
			pos.x += 8.0f;
			break;
		case ' ':
			++pos.x;
			break;
		default:
			cur_char->glyph = c;
			cur_char->pos = pos;
			cur_char->color = color;
			++index;
			++cur_char;
			++pos.x;
			break;
		}
	}
	ASSERT(index <= IMGUI_MAX_TEXT_CHARACTERS);
	context->text_index = index;
	context->text_pos.y += pos.y - context->text_pos.y + 1;
}

u8 imgui_tree_begin(IMGUI_Context* context, char* name)
{
	u32 name_len = 2; // 2 chars for prefix
	for (char *p = name; *p; ++p) {
		++name_len;
	}
	v2 glyph_size = 2.0f * v2((f32)TEXTURE_CODEPAGE_437.glyph_width,
	                          (f32)TEXTURE_CODEPAGE_437.glyph_height);
	v2 top_left = context->text_pos * glyph_size;
	v2 bottom_right = (context->text_pos + v2((f32)name_len, 1.0f)) * glyph_size;

	v4_f32 color = context->text_color;

	v2 mouse_pos = (v2)context->input->mouse_pos;
	u8 mouse_over_element = mouse_pos.x > top_left.x && mouse_pos.x < bottom_right.x
                             && mouse_pos.y > top_left.y && mouse_pos.y < bottom_right.y;
	u32 mouse_pressed = input_get_num_down_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);
	u32 mouse_released = input_get_num_up_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);

	uptr id = (uptr)name;
	IMGUI_Element_State *element_state = NULL;
	auto& element_states = context->element_states;
	for (u32 i = 0; i < element_states.len; ++i) {
		if (element_states[i].id == id) {
			element_state = &element_states[i];
			break;
		}
	}
	if (element_state == NULL) {
		++element_states.len;
		element_state = &element_states[element_states.len - 1];
		element_state->id = id;
		// element_state->tree_begin.collapsed = 1;
	}

	uptr hot_element_id = context->hot_element_id;
	if (!hot_element_id) {
		if (mouse_over_element && mouse_pressed) {
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
			context->hot_element_id = id;
		} else if (mouse_over_element) {
			context->text_color = v4(0.0f, 1.0f, 1.0f, 1.0f);
		} else {
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
		}
	} else if (hot_element_id == id) {
		if (mouse_released) {
			element_state->tree_begin.collapsed = !element_state->tree_begin.collapsed;
			context->hot_element_id = 0;
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
		} else {
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
		}
	} else {
		context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
	}

	u8 result = element_state->tree_begin.collapsed;

	char buffer[1024];
	char *prefix = result ? "- " : "+ ";
	snprintf(buffer, ARRAY_SIZE(buffer), "%s%s", prefix, name);

	imgui_text(context, buffer);
	if (result) {
		context->text_pos.x += 4;
		++context->tree_indent_level;
	}

	context->text_color = color;
	return result;
}

void imgui_tree_end(IMGUI_Context* context)
{
	context->text_pos.x -= 4;
	--context->tree_indent_level;
}

void imgui_f32(IMGUI_Context* context, char* name, f32* value, f32 min_val, f32 max_val)
{
	char buffer[1024];
	snprintf(buffer, ARRAY_SIZE(buffer), "%s: %f", name, *value);
	u32 label_len = 0;
	for (char *p = buffer; *p; ++p, ++label_len);

	v2 glyph_size = 2.0f * v2((f32)TEXTURE_CODEPAGE_437.glyph_width,
	                          (f32)TEXTURE_CODEPAGE_437.glyph_height);

	v2 top_left = context->text_pos * glyph_size;
	v2 bottom_right = (context->text_pos + v2((f32)label_len, 1.0f)) * glyph_size;

	v2 mouse_pos = (v2)context->input->mouse_pos;
	u8 mouse_over_element = mouse_pos.x > top_left.x && mouse_pos.x < bottom_right.x
                             && mouse_pos.y > top_left.y && mouse_pos.y < bottom_right.y;
	u32 mouse_pressed = input_get_num_down_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);
	u32 mouse_released = input_get_num_up_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);

	uptr id = (uptr)value;
	uptr hot_element_id = context->hot_element_id;

	if (!hot_element_id) {
		if (mouse_over_element && mouse_pressed) {
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
			context->hot_element_id = id;
			// *value += 1;
		} else if (mouse_over_element) {
			context->text_color = v4(0.0f, 1.0f, 1.0f, 1.0f);
		} else {
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
		}
	} else if (hot_element_id == id) {
		if (mouse_released) {
			context->hot_element_id = 0;
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
		} else {
			f32 delta = (max_val - min_val) * (f32)context->input->mouse_delta.x / 400.0f;
			*value = clamp(*value + delta, min_val, max_val);
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
		}
	} else {
		context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
	}

	imgui_text(context, buffer);
}

void imgui_u32(IMGUI_Context* context, char* name, u32* value, u32 min_val, u32 max_val)
{
	char buffer[1024];
	snprintf(buffer, ARRAY_SIZE(buffer), "%s: %u", name, *value);
	u32 label_len = 0;
	for (char *p = buffer; *p; ++p, ++label_len);

	v2 glyph_size = 2.0f * v2((f32)TEXTURE_CODEPAGE_437.glyph_width,
	                          (f32)TEXTURE_CODEPAGE_437.glyph_height);

	v2 top_left = context->text_pos * glyph_size;
	v2 bottom_right = (context->text_pos + v2((f32)label_len, 1.0f)) * glyph_size;

	v2 mouse_pos = (v2)context->input->mouse_pos;
	u8 mouse_over_element = mouse_pos.x > top_left.x && mouse_pos.x < bottom_right.x
                             && mouse_pos.y > top_left.y && mouse_pos.y < bottom_right.y;
	u32 mouse_pressed = input_get_num_down_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);
	u32 mouse_released = input_get_num_up_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);

	uptr id = (uptr)value;
	uptr hot_element_id = context->hot_element_id;

	if (!hot_element_id) {
		if (mouse_over_element && mouse_pressed) {
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
			context->hot_element_id = id;
			// *value += 1;
		} else if (mouse_over_element) {
			context->text_color = v4(0.0f, 1.0f, 1.0f, 1.0f);
		} else {
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
		}
	} else if (hot_element_id == id) {
		if (mouse_released) {
			context->hot_element_id = 0;
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
		} else {
			f32 delta = (max_val - min_val) * (f32)context->input->mouse_delta.x / 400.0f;
			f32 val = clamp((f32)*value + delta, (f32)min_val, (f32)max_val);
			*value = (u32)val;
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
		}
	} else {
		context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
	}

	imgui_text(context, buffer);
}

u8 imgui_button(IMGUI_Context* context, char* caption)
{
	u32 label_len = 0;
	for (char *p = caption; *p; ++p, ++label_len);

	v2 glyph_size = 2.0f * v2((f32)TEXTURE_CODEPAGE_437.glyph_width,
	                          (f32)TEXTURE_CODEPAGE_437.glyph_height);

	v2 top_left = context->text_pos * glyph_size;
	v2 bottom_right = (context->text_pos + v2((f32)label_len, 1.0f)) * glyph_size;

	v2 mouse_pos = (v2)context->input->mouse_pos;
	u8 mouse_over_element = mouse_pos.x > top_left.x && mouse_pos.x < bottom_right.x
                             && mouse_pos.y > top_left.y && mouse_pos.y < bottom_right.y;
	u32 mouse_pressed = input_get_num_down_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);
	u32 mouse_released = input_get_num_up_transitions(context->input, INPUT_BUTTON_MOUSE_LEFT);

	uptr id = (uptr)caption;
	uptr hot_element_id = context->hot_element_id;

	u8 result = 0;
	if (!hot_element_id) {
		if (mouse_over_element && mouse_pressed) {
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
			context->hot_element_id = id;
			// *value += 1;
		} else if (mouse_over_element) {
			context->text_color = v4(0.0f, 1.0f, 1.0f, 1.0f);
		} else {
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
		}
	} else if (hot_element_id == id) {
		if (mouse_released) {
			context->hot_element_id = 0;
			context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
			result = 1;
		} else {
			context->text_color = v4(1.0f, 1.0f, 0.0f, 1.0f);
		}
	} else {
		context->text_color = v4(1.0f, 0.0f, 1.0f, 1.0f);
	}

	imgui_text(context, caption);

	return result;
}

void render(IMGUI_Context* context, Render* renderer)
{
	auto r = &renderer->render_job_buffer;
	Sprite_Constants constants = {};
	constants.tile_input_size = v2(9.0f, 16.0f);
	constants.tile_output_size = 2.0f * constants.tile_input_size;
	begin_sprites(r, SOURCE_TEXTURE_CODEPAGE437, constants);

	for (u32 i = 0; i < context->text_index; ++i) {
		auto glyph = &context->text_buffer[i];
		Sprite_Instance instance = {};
		instance.glyph_coords = v2(glyph->glyph % 32, glyph->glyph / 32);
		instance.output_coords = glyph->pos;
		instance.color = glyph->color;
		push_sprite(r, instance);
	}
}

#include "input.h"

#include "stdafx.h"

void input_reset(Input* input)
{
	input->len = 0;
	input->mouse_wheel_delta = 0;
	for (u32 i = 0; i < NUM_INPUT_BUTTONS; ++i) {
		Input_Button_Frame_Data *button = &input->button_data[i];
		u16 new_flags = 0;
		if (button->flags & (INPUT_BUTTON_FLAG_ENDED_DOWN | INPUT_BUTTON_FLAG_HELD_DOWN)) {
			new_flags |= INPUT_BUTTON_FLAG_STARTED_DOWN | INPUT_BUTTON_FLAG_HELD_DOWN;
		}
		button->flags = new_flags;
		button->num_transitions = 0;
	}
}

void input_button_down(Input* input, Input_Button button)
{
	Input_Button_Frame_Data *b = &input->button_data[button];
	b->flags |= INPUT_BUTTON_FLAG_ENDED_DOWN;
	++b->num_transitions;
}

void input_button_up(Input* input, Input_Button button)
{
	Input_Button_Frame_Data *b = &input->button_data[button];
	b->flags &= ~(INPUT_BUTTON_FLAG_ENDED_DOWN | INPUT_BUTTON_FLAG_HELD_DOWN);
	++b->num_transitions;
}

void input_push(Input* input, Input_Event event)
{
	ASSERT(input->len < INPUT_MAX_EVENTS);
	input->event[input->len++] = event;
}

u32 input_get_num_down_transitions(Input* input, Input_Button button)
{
	Input_Button_Frame_Data *button_data = &input->button_data[button];
	u32 transitions = button_data->num_transitions;
	if (!(button_data->flags & INPUT_BUTTON_FLAG_STARTED_DOWN)) {
		++transitions;
	}
	return transitions / 2;
}

u32 input_get_num_up_transitions(Input* input, Input_Button button)
{
	Input_Button_Frame_Data *button_data = &input->button_data[button];
	u32 transitions = button_data->num_transitions;
	if (button_data->flags & INPUT_BUTTON_FLAG_STARTED_DOWN) {
		++transitions;
	}
	return transitions / 2;
}

struct Move
{
	Pos start;
	Pos end;
};

struct Delay
{
	Entity_ID waiting_entity_id;
	Entity_ID wait_for_entity_id;
	Pos start;
	Pos end;
};

void choose_actions(Slice<Controller> controllers)
{
	Max_Length_Array<MAX_ENTITIES, Action> chosen_actions;
	chosen_actions.reset();

	Max_Length_Array<MAX_ENTITIES, Delay> delay_list;
	delay_list.reset();

	Bit_Array<MAX_ENTITIES> is_delaying, has_moved;
	is_delaying.reset();
	has_moved.reset();

	for (u32 i = 0; i < controllers.len; ++i) {
		Controller *controller = &controllers[i];
		Slice<Action> actions = controller.get_moves(delay_list);
		for (u32 i = 0; i < actions.len; ++i) {
			Action *action = &actions[i];
			switch (action->type) {
			case ACTION_DELAY_MOVE:
				delay_list.push(action->entity_id);
				is_delaying.set(action->entity_id);
				break;
			case ACTION_MOVE:
				chosen_actions.append(*action);
				has_moved.set(action->entity_id);
				break;
			case ACTION_NONE:
				break;
			default:
				ASSERT(0);
			}
		}
	}

	// draw moves made so far
	debug_draw_reset();
	for (u32 i = 0; i < chosen_actions.len; ++i) {
		Action *a = &chosen_actions[i];
		debug_world_draw_arrow(a->move.start, a->move.end);
	}
	debug_pause();

	Max_Length_Array<MAX_ENTITIES, Entity_ID> entities_that_cant_move;
	entities_that_cant_move.reset();

	// move elements with no dependencies to the start
	for (u32 index = 0, counter = 0; counter < delay_list.len; ) {
		Delay delay = delay_list[index];
		if (is_delaying.get(delay.wait_for_entity_id)) {
			++index;
			++counter;
		} else {
			is_delaying.unset(delay.waiting_entity_id);
			if (has_moved.get(delay.wait_for_entity_id)) {
				Action move = {};
				move.type = ACTION_MOVE;
				move.entity_id = delay.waiting_entity_id;
				move.move.start = delay.start;
				move.move.end = delay.end;
				chosen_actions.append(move);
			} else {
				entities_that_cant_move.append(delay.waiting_entity_id);
			}
			delay_list.remove(index);
			counter = 0;
		}
		index %= delay_list.len;
	}

	debug_pause();
}

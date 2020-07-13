BEGIN_SECTION(z_offsets)
	F32(floor,             0.0f, 0.0f, 2.0f)
	F32(wall,              1.0f, 0.0f, 2.0f)
	F32(wall_shadow,       0.1f, 0.0f, 2.0f)
	F32(character,         1.0f, 0.0f, 2.0f)
	F32(character_shadow,  0.9f, 0.0f, 2.0f)
	F32(water_edge,       0.05f, 0.0f, 2.0f)
	F32(item,             0.15f, 0.0f, 2.0f)
END_SECTION(z_offsets)
BEGIN_SECTION(anims)
	F32(text_duration, 0.5f, 0.25f, 2.0f)
	BEGIN_SECTION(drop_tile)
		F32(duration,      0.5f, 0.25f, 1.0f)
		F32(drop_distance, 1.0f,  0.5f, 2.0f)
	END_SECTION(drop_tile)
	BEGIN_SECTION(move)
		F32(duration,    0.5f, 0.25f,  2.0f)
		F32(jump_height, 3.0f, 0.0f,  24.0f)
	END_SECTION(move)
	BEGIN_SECTION(fireball)
		F32(shake_duration, 0.5f, 0.25f,  1.0f)
		F32(shake_power,    1.0f,  0.5f,  5.0f)
		F32(shot_duration,  0.5f, 0.25f,  2.0f)
		F32(min_speed,     10.0f,  2.5f, 10.0f)
	END_SECTION(fireball)
	BEGIN_SECTION(exchange)
		F32(cast_time, 0.25f, 0.0f, 1.0f)
	END_SECTION(exchange)
	BEGIN_SECTION(blink)
		F32(cast_time,          0.25f, 0.0f, 1.0f)
		F32(particle_start,     0.25f, 0.0f, 1.0f)
		F32(particle_duration,   0.5f, 0.0f, 1.0f)
		U32(num_particles,         64,   32, 128)
	END_SECTION(blink)
	BEGIN_SECTION(poison)
		F32(cast_time, 0.25f, 0.0f, 1.0f)
	END_SECTION(poison)
	BEGIN_SECTION(slime_split)
		F32(duration, 0.25f, 0.0f, 1.0f)
	END_SECTION(slime_split)
	BEGIN_SECTION(fire_bolt)
		F32(cast_time, 0.25f, 0.0f,  1.0f)
		F32(speed,     25.0f, 0.0f, 50.0f)
	END_SECTION(fire_bolt)
END_SECTION(anims)
BEGIN_SECTION(cards_ui)
	F32(draw_jump_height,             0.3f,  0.0f, 1.0f)
	F32(draw_duration,                0.6f,  0.3f, 1.0f)
	F32(between_draw_delay,           0.1f,  0.0f, 1.0f)
	F32(min_radius,                   0.2f,  0.0f, 1.0f)
	F32(normal_to_selected_duration,  0.1f, 0.0f, 1.0f)
	F32(hand_to_hand_time,            0.1f,  0.0f, 0.5f)
	F32(hand_to_in_play_time,         0.5f,  0.0f, 1.0f)
	F32(hand_to_discard_time,         0.5f,  0.0f, 1.0f)
	F32(in_play_to_discard_time,     0.25f,  0.0f, 1.0f)
	F32(height,                       0.5f,  0.0f, 1.0f)
	F32(border,                       0.4f,  0.0f, 1.0f)
	F32(top,                         -0.7f, -1.0f, 0.0f)
	F32(bottom,                      -0.9f, -1.0f, 0.0f)
	F32(selection_fade,               0.7f,  0.0f, 1.0f)
END_SECTION(cards_ui)

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
	BEGIN_SECTION(move)
		F32(duration, 0.5f, 0.25f, 2.0f)
	END_SECTION(move)
	BEGIN_SECTION(fireball)
		F32(shot_duration,  0.5f, 0.25f,  2.0f)
		F32(min_speed,     10.0f,  2.5f, 10.0f)
	END_SECTION(fireball)
END_SECTION(anims)

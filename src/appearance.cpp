#include "appearance.h"

u8 appearance_is_creature(Appearance appearance)
{
	return appearance >= APPEARANCE_CREATURE_MALE_KNIGHT && appearance <= APPEARANCE_CREATURE_SPIDER_GREEN;
}

u8 appearance_is_wall(Appearance appearance)
{
	return appearance >= APPEARANCE_WALL_ROCK_1 && appearance <= APPEARANCE_WALL_RUINED_LOW_RED;
}

u8 appearance_is_floor(Appearance appearance)
{
	return appearance >= APPEARANCE_FLOOR_ROCK && appearance <= APPEARANCE_FLOOR_ROCK;
}

u8 appearance_is_door(Appearance appearance)
{
	return appearance >= APPEARANCE_DOOR_WOODEN_PLAIN && appearance <= APPEARANCE_DOOR_GLASS;
}

u8 appearance_is_liquid(Appearance appearance)
{
	return appearance >= APPEARANCE_LIQUID_WATER && appearance <= APPEARANCE_LIQUID_SEWAGE;
}

u8 appearance_is_item(Appearance appearance)
{
	return appearance >= APPEARANCE_ITEM_GRAVESTONE && appearance <= APPEARANCE_ITEM_VASE_4_BROKEN;
}

v2 appearance_get_creature_sprite_coords(Appearance appearance)
{
	v2 result = {};
	switch (appearance) {
	case APPEARANCE_CREATURE_SPIDER_BLUE:
		result = v2(19.0f, 12.0f);
		break;
	case APPEARANCE_CREATURE_SPIDER_GREEN:
		result = v2(18.0f, 12.0f);
		break;
	default:
		u32 index = appearance - APPEARANCE_CREATURE_MALE_KNIGHT;
		result = v2((f32)(index % 18), 2.0f * (f32)(index / 18));
		break;
	}
	return result;
}

static const u8 OFFSET_LOOKUP[] = { 0, 6, 1, 9, 4, 5, 7, 14, 3, 10, 2, 15, 8, 13, 12, 11 };
v2 appearance_get_wall_sprite_coords(Appearance appearance, u8 connection_mask)
{
	v2 result = {};
	result.y = (f32)(appearance - APPEARANCE_WALL_ROCK_1);
	result.x = 9.0f + (f32)OFFSET_LOOKUP[connection_mask];
	return result;
}

u8 appearance_get_wall_id(Appearance appearance)
{
	return (u8)(appearance - APPEARANCE_WALL_ROCK_1 + 1);
}

Appearance appearance_wall_id_to_appearance(u8 wall_id)
{
	return (Appearance)(wall_id + APPEARANCE_WALL_ROCK_1 - 1);
}

static const v2 FLOOR_SPRITE_COORDS_LOOKUP[] = {
	{ 3.0f, 0.0f }
};
v2 appearance_get_floor_sprite_coords(Appearance appearance)
{
	return FLOOR_SPRITE_COORDS_LOOKUP[appearance - APPEARANCE_FLOOR_ROCK];
}

u8 appearance_get_floor_id(Appearance appearance)
{
	return (u8)(appearance - APPEARANCE_FLOOR_ROCK + 1);
}

Appearance appearance_floor_id_to_appearance(u8 floor_id)
{
	return (Appearance)(floor_id + APPEARANCE_FLOOR_ROCK - 1);
}

static const v2 DOOR_SPRITE_COORDS_LOOKUP[] = {
	{ 28.0f, 2.0f },
	{ 29.0f, 2.0f },
	{ 30.0f, 2.0f },
	{ 31.0f, 2.0f },
	{ 32.0f, 2.0f },
	{ 33.0f, 2.0f },
	{ 34.0f, 2.0f },
	{ 35.0f, 2.0f },
	{ 36.0f, 2.0f },
	{ 37.0f, 2.0f },
	{ 38.0f, 2.0f },
	{ 39.0f, 2.0f },
	{ 40.0f, 2.0f },
	{ 41.0f, 2.0f },
	{ 28.0f, 3.0f },
	{ 29.0f, 3.0f },
	{ 30.0f, 3.0f }
};
v2 appearance_get_door_sprite_coords(Appearance appearance)
{
	return DOOR_SPRITE_COORDS_LOOKUP[appearance - APPEARANCE_DOOR_WOODEN_PLAIN];
}

static const v2 LIQUID_SPRITE_COORDS_LOOKUP[] = {
	{ 4.0f, 30.0f },
	{ 7.0f, 30.0f },
	{ 10.0f, 30.0f },
	{ 13.0f, 30.0f }
};
v2 appearance_get_liquid_sprite_coords(Appearance appearance)
{
	return LIQUID_SPRITE_COORDS_LOOKUP[appearance - APPEARANCE_LIQUID_WATER];
}

u8 appearance_get_liquid_id(Appearance appearance)
{
	return (u8)(appearance - APPEARANCE_LIQUID_WATER + 1);
}

static const v2 ITEM_SPRITE_COORDS_LOOKUP[] = {
	{ 28.0f, 0.0f },
	{ 29.0f, 0.0f },
	{ 30.0f, 0.0f },
	{ 31.0f, 0.0f },
	{ 32.0f, 0.0f },
	{ 33.0f, 0.0f },
	{ 34.0f, 0.0f },
	{ 35.0f, 0.0f },
	{ 36.0f, 0.0f },
	{ 37.0f, 0.0f },
	{ 38.0f, 0.0f },
	{ 39.0f, 0.0f },
	{ 40.0f, 0.0f },
	{ 41.0f, 0.0f },
	{ 28.0f, 1.0f },
	{ 29.0f, 1.0f },
	{ 30.0f, 1.0f },
	{ 31.0f, 1.0f },
	{ 32.0f, 1.0f },
	{ 33.0f, 1.0f },
	{ 40.0f, 1.0f },
	{ 41.0f, 1.0f },
	{ 31.0f, 3.0f },
	{ 32.0f, 3.0f },
	{ 33.0f, 3.0f },
	{ 34.0f, 3.0f },
	{ 35.0f, 3.0f },
	{ 36.0f, 3.0f },
	{ 37.0f, 3.0f },
	{ 38.0f, 3.0f },
	{ 39.0f, 3.0f },
	{ 40.0f, 3.0f },
	{ 41.0f, 3.0f },
	{ 28.0f, 4.0f },
	{ 29.0f, 4.0f },
	{ 30.0f, 4.0f },
	{ 31.0f, 4.0f },
	{ 32.0f, 4.0f },
	{ 33.0f, 4.0f },
	{ 34.0f, 4.0f },
	{ 35.0f, 4.0f },
	{ 36.0f, 4.0f },
	{ 37.0f, 4.0f },
	{ 38.0f, 4.0f },
	{ 39.0f, 4.0f },
	{ 40.0f, 4.0f },
	{ 41.0f, 4.0f },
	{ 30.0f, 5.0f },
	{ 31.0f, 5.0f },
	{ 32.0f, 5.0f },
	{ 33.0f, 5.0f },
	{ 34.0f, 5.0f },
	{ 35.0f, 5.0f },
	{ 29.0f, 6.0f },
	{ 29.0f, 6.0f },
	{ 30.0f, 6.0f },
	{ 31.0f, 6.0f },
	{ 32.0f, 6.0f },
	{ 33.0f, 6.0f },
	{ 34.0f, 6.0f },
	{ 35.0f, 6.0f },
	{ 36.0f, 6.0f },
	{ 37.0f, 6.0f },
	{ 38.0f, 6.0f },
	{ 39.0f, 6.0f },
	{ 40.0f, 6.0f },
	{ 41.0f, 6.0f },
	{ 29.0f, 7.0f },
	{ 30.0f, 7.0f },
	{ 31.0f, 7.0f },
	{ 32.0f, 7.0f },
	{ 33.0f, 7.0f },
	{ 34.0f, 7.0f },
	{ 35.0f, 7.0f },
	{ 36.0f, 7.0f },
	{ 37.0f, 7.0f },
	{ 38.0f, 7.0f },
	{ 39.0f, 7.0f },
	{ 40.0f, 7.0f },
	{ 41.0f, 7.0f },
	{ 42.0f, 7.0f }
};
v2 appearance_get_item_sprite_coords(Appearance appearance)
{
	return ITEM_SPRITE_COORDS_LOOKUP[appearance - APPEARANCE_ITEM_GRAVESTONE];
}
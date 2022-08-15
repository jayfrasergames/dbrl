#include "level_gen.h"

#include "stdafx.h"
#include "random.h"

static thread_local Log *thread_log = NULL;
static void log(const char* str)
{
	if (thread_log) {
		log(thread_log, str);
	}
}
static void logf(const char* fmt_str, ...)
{
	if (!thread_log) {
		return;
	}
	char print_buffer[4096];
	va_list args;
	va_start(args, fmt_str);
	vsnprintf(print_buffer, ARRAY_SIZE(print_buffer), fmt_str, args);
	va_end(args);

	log(thread_log, print_buffer);
}

static void log_map(Map_Cache_Bool* map, v2_u32 size)
{
	char buffer[256] = {};
	for (u32 y = 0; y < size.h; ++y) {
		for (u32 x = 0; x < size.w; ++x) {
			Pos p = Pos(x, y);
			if (map->get(p)) {
				buffer[x] = '#';
			} else {
				buffer[x] = '.';
			}
		}
		log(buffer);
	}
}

// Rooms always assumed to have borders

static void move_to_top_left(Map_Cache_Bool* map, v2_u32* size)
{
	v2_u32 orig_size = *size;
	v2_u32 offset = v2_u32(256, 256);
	for (u32 y = 0; y < orig_size.h; ++y) {
		for (u32 x = 0; x < orig_size.w; ++x) {
			Pos p = Pos(x, y);
			if (!map->get(p)) {
				offset.x = min_u32(offset.x, x);
				offset.y = min_u32(offset.y, y);
			}
		}
	}

	// *size -= offset;
	v2_u32 new_size = *size - offset;
	offset -= v2_u32(1, 1);
	for (u32 y = 0; y < new_size.h; ++y) {
		for (u32 x = 0; x < new_size.w; ++x) {
			Pos old_p = Pos(x, y) + (Pos)offset;
			Pos new_p = Pos(x, y);
			if (map->get(old_p)) {
				map->set(new_p);
			} else {
				map->unset(new_p);
			}
		}
	}

	for (u32 y = 0; y < new_size.h; ++y) {
		for (u32 x = new_size.w; x < orig_size.w; ++x) {
			Pos p = Pos(x, y);
			map->set(p);
		}
	}
	for (u32 y = new_size.h; y < orig_size.h; ++y) {
		for (u32 x = 0; x < orig_size.w; ++x) {
			Pos p = Pos(x, y);
			map->set(p);
		}
	}
}

static void biggest_connected_component(Map_Cache_Bool* map, v2_u32 size)
{
	Map_Cache<u32> component_map;
	memset(&component_map, 0, sizeof(component_map));
	u32 component_keys[256*256] = {};
	u32 component_counts[256*256] = {};
	u32 num_components = 0;

	for (u32 y = 1; y < size.h - 1; ++y) {
		for (u32 x = 1; x < size.w - 1; ++x) {
			Pos p = Pos(x, y);
			if (map->get(p)) {
				continue;
			}
			u32 this_component = 0;
			u32 left_component = component_map[p - Pos(1, 0)];
			u32 above_component = component_map[p - Pos(0, 1)];
			if (!left_component && !above_component) {
				this_component = ++num_components;
				component_keys[this_component] = this_component;
			} else if (!left_component) {
				this_component = above_component;
			} else if (!above_component) {
				this_component = left_component;
			} else if (left_component == above_component) {
				this_component = left_component;
			} else if (left_component < above_component) {
				this_component = left_component;
				component_keys[above_component] = left_component;
			} else {
				this_component = above_component;
				component_keys[left_component] = above_component;
			}
			component_map[p] = this_component;
			++component_counts[this_component];
		}
	}

	u32 best_count = 0;
	u32 best_key = 0;
	for (u32 i = 1; i <= num_components; ++i) {
		u32 count = component_counts[i];
		u32 key = i;
		while (component_keys[key] != key) {
			key = component_keys[key];
		}
		if (key != i) {
			component_keys[i] = key;
			component_counts[key] += count;
		}
		count = component_counts[key];
		if (count > best_count) {
			best_count = count;
			best_key = key;
		}
	}

	for (u32 y = 1; y < size.h - 1; ++y) {
		for (u32 x = 1; x < size.w - 1; ++x) {
			Pos p = Pos(x, y);
			if (component_keys[component_map[p]] != best_key) {
				map->set(p);
			}
		}
	}
}

static void cellular_automata(Map_Cache_Bool* map, v2_u32* out_size, f32 wall_chance, u32 remain_wall_count, u32 become_wall_count, u32 num_iters, f32 minimum_area, u32 max_attempts)
{
	// set bits represent walls
	// unset bits are clear
	// the border bits are guaranteed to be walls

	v2_u32 size = *out_size;

	f32 area = 0.0f;
	do {
		map->reset();
		for (u32 x = 0; x < size.w; ++x) {
			map->set(Pos(x,          0));
			map->set(Pos(x, size.h - 1));
		}
		for (u32 y = 0; y < size.h; ++y) {
			map->set(Pos(         0, y));
			map->set(Pos(size.w - 1, y));
		}

		for (u32 y = 1; y < size.h - 1; ++y) {
			for (u32 x = 1; x < size.w - 1; ++x) {
				if (rand_f32() > wall_chance) {
					map->set(Pos(x, y));
				}
			}
		}

		for (u32 i = 0; i < num_iters; ++i) {
			for (u32 y = 1; y < size.h - 1; ++y) {
				for (u32 x = 1; x < size.w - 1; ++x) {
					u32 wall_count = 0;
					if (map->get(Pos(x - 1, y - 1))) { ++wall_count; }
					if (map->get(Pos(    x, y - 1))) { ++wall_count; }
					if (map->get(Pos(x + 1, y - 1))) { ++wall_count; }
					if (map->get(Pos(x - 1,     y))) { ++wall_count; }
					if (map->get(Pos(x + 1,     y))) { ++wall_count; }
					if (map->get(Pos(x - 1, y + 1))) { ++wall_count; }
					if (map->get(Pos(    x, y + 1))) { ++wall_count; }
					if (map->get(Pos(x + 1, y + 1))) { ++wall_count; }

					if (map->get(Pos(x, y))) {
						if (wall_count < remain_wall_count) {
							map->unset(Pos(x, y));
						}
					} else {
						if (wall_count >= become_wall_count) {
							map->set(Pos(x, y));
						}
					}
				}
			}
		}

		biggest_connected_component(map, size);

		f32 num_floors = 0.0f;
		for (u32 y = 1; y < size.h - 1; ++y) {
			for (u32 x = 1; x < size.w - 1; ++x) {
				if (!map->get(Pos(x, y))) {
					++num_floors;
				}
			}
		}
		area = num_floors / (f32)(size.w * size.h);
	} while (area < minimum_area && --max_attempts);

	move_to_top_left(map, out_size);
}

// struct Room
struct Room
{
	v2_u32 top_left;
	v2_u32 size;
};

static void make_spider_room(Game* game, Room* room)
{
	Map_Cache_Bool map = {};
	cellular_automata(&map, &room->size, 0.5f, 4, 5, 4, 0.4f, 20);

	Pos player_pos = {};
	u32 floors_seen = 0;
	for (u32 y = 0; y < room->size.h; ++y) {
		for (u32 x = 0; x < room->size.w; ++x) {
			Pos p = Pos(x, y);
			Pos real_pos = p + (Pos)room->top_left;
			Tile t = {};
			if (map.get(p)) {
				t.type = TILE_WALL;
				t.appearance = APPEARANCE_WALL_BROWN_ROCK;
			} else {
				t.type = TILE_FLOOR;
				t.appearance = APPEARANCE_FLOOR_ROCK;
				if ((rand_u32() % ++floors_seen) == 0) {
					player_pos = real_pos;
				}
			}
			game->tiles[real_pos] = t;
		}
	}

	auto p = get_player(game);
	p->pos = player_pos;
}

void build_level_spider_room(Game* game, Log* l)
{
	thread_log = l;

	init(game);

	Room room = {};
	room.top_left = v2_u32(10, 10);
	room.size = v2_u32(40, 40);
	make_spider_room(game, &room);

	update_fov(game);
}

// static void add_creature(Game* game)

static void from_string(Game* game, char* str)
{
	init(game);

	Pos cur_pos = Pos(1, 1);

	auto& tiles = game->tiles;

	Controller lich_controller_tmp = {};
	lich_controller_tmp.type = CONTROLLER_LICH;
	Controller *lich_controller = &lich_controller_tmp;

	for (char *p = str; *p; ++p) {
		switch (*p) {
		case '\n':
			cur_pos.x = 1;
			++cur_pos.y;
			continue;
		case '#': {
			tiles[cur_pos].type = TILE_WALL;
			tiles[cur_pos].appearance = APPEARANCE_WALL_WOOD;
			break;
		}
		case 'x': {
			tiles[cur_pos].type = TILE_WALL;
			tiles[cur_pos].appearance = APPEARANCE_WALL_FANCY;
			break;
		}
		case '.': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;
			break;
		}
		case 'w': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto e = add_entity(game);
			e->hit_points = 1;
			e->max_hit_points = 1;
			e->pos = cur_pos;
			e->appearance = rand_u32() % 2 ?  APPEARANCE_ITEM_SPIDERWEB_1 : APPEARANCE_ITEM_SPIDERWEB_2;

			auto mh = add_message_handler(game);
			mh->type = MESSAGE_HANDLER_PREVENT_EXIT;
			mh->handle_mask = MESSAGE_MOVE_PRE_EXIT;
			mh->owner_id = e->id;
			mh->prevent_exit.pos = cur_pos;

			break;
		}
		case 's': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			add_slime(game, cur_pos, 5);

			break;
		}
		case 'L': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto e = add_entity(game);
			e->hit_points = 10;
			e->max_hit_points = 10;
			e->pos = cur_pos;
			e->appearance = APPEARANCE_CREATURE_NECROMANCER;
			e->block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e->movement_type = BLOCK_WALK;

			lich_controller = add_controller(game);
			memcpy(lich_controller, &lich_controller_tmp, sizeof(lich_controller_tmp));
			lich_controller->lich.lich_id = e->id;

			auto mh = add_message_handler(game);
			mh->type = MESSAGE_HANDLER_LICH_DEATH;
			mh->handle_mask = MESSAGE_PRE_DEATH;
			mh->owner_id = e->id;
			mh->lich_death.controller_id = lich_controller->id;

			break;
		}
		case 'S': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto e = add_entity(game);
			e->hit_points = 10;
			e->max_hit_points = 10;
			e->pos = cur_pos;
			e->appearance = APPEARANCE_CREATURE_SKELETON;
			e->block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e->movement_type = BLOCK_WALK;

			lich_controller->lich.skeleton_ids.append(e->id);
			break;
		}
		case 'd': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto e = add_entity(game);
			e->hit_points = 100;
			e->max_hit_points = 100;
			e->pos = cur_pos;
			e->appearance = APPEARANCE_CREATURE_RED_DRAGON;
			e->block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e->movement_type = BLOCK_WALK;

			auto c = add_controller(game);
			c->type = CONTROLLER_DRAGON;
			c->dragon.entity_id = e->id;

			break;
		}
		case '^': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto e = add_entity(game);
			e->hit_points = 1;
			e->max_hit_points = 1;
			e->pos = cur_pos;
			e->appearance = APPEARANCE_ITEM_TRAP_HEX;

			auto mh = add_message_handler(game);
			mh->type = MESSAGE_HANDLER_TRAP_FIREBALL;
			mh->handle_mask = MESSAGE_MOVE_POST_ENTER;
			mh->owner_id = e->id;
			mh->trap.pos = cur_pos;

			break;
		}
		case 'v': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto mh = add_message_handler(game);
			mh->type = MESSAGE_HANDLER_DROP_TILE;
			mh->handle_mask = MESSAGE_MOVE_POST_EXIT;
			mh->trap.pos = cur_pos;

			break;
		}
		case '@': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto p = get_player(game);
			p->pos = cur_pos;

			break;
		}
		case 'b': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto e = add_entity(game);
			e->hit_points = 5;
			e->max_hit_points = 5;
			e->pos = cur_pos;
			e->movement_type = BLOCK_FLY;
			e->block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e->appearance = APPEARANCE_CREATURE_RED_BAT;
			e->default_action = ACTION_BUMP_ATTACK;

			auto c = add_controller(game);
			c->type = CONTROLLER_RANDOM_MOVE;
			c->random_move.entity_id = e->id;

			break;
		}
		case '~': {
			tiles[cur_pos].type = TILE_WATER;
			tiles[cur_pos].appearance = APPEARANCE_LIQUID_WATER;
			break;
		}
		case '+': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			auto e = add_entity(game);
			e->hit_points = 5;
			e->max_hit_points = 5;
			e->pos = cur_pos;
			e->block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e->appearance = APPEARANCE_DOOR_WOODEN_PLAIN;
			e->blocks_vision = true;
			e->default_action = ACTION_OPEN_DOOR;

			break;
		}

		}
		++cur_pos.x;
	}

	update_fov(game);
}

void build_level_default(Game* game, Log* log)
{
	from_string(game,
		"##########################################\n"
		"#.........b...#b#........................#\n"
		"#......#.bwb..#b#........................#\n"
		"#.....###.b...#b#.......###..............#\n"
		"#....#####....#b#.......#x#..............#\n"
		"#...##.#.##...#b#.....###x#.#............#\n"
		"#..##..#..##..#b#.....#xxxxx#............#\n"
		"#...b..#......#b#.....###x###............#\n"
		"#b.bwb.#......#w#.......#x#..............#\n"
		"#wb.b..#...b..@.........###..............#\n"
		"#b.....#..bwb.#..........................#\n"
		"#......#...b..#vvv.^..^....#.............#\n"
		"#......#......#vvv.......................#\n"
		"#~~~...#.....x#vvv.^...^.....#...........#\n"
		"#~~~~..#.....x#vvv.......................#\n"
		"#~~~~~~..xxxxx#............#...#.........#\n"
		"##########x####..........................#\n"
		"#.............#........#.................#\n"
		"#.............#.......##.................#\n"
		"#.............#......##.##...............#\n"
		"#.............#.......##.................#\n"
		"#.............#........#.................#\n"
		"#.............+..........~~..............#\n"
		"#.............#.........~~~~~............#\n"
		"#.............#.........~...~~~..........#\n"
		"#.............#.........~~.~~.~..........#\n"
		"#.............#............~.~...........#\n"
		"#.............#...........~~~~...........#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"##########################################\n");
}

void build_level_anim_test(Game* game, Log* log)
{
	from_string(game,
		"###########\n"
		"#b#b#b#b#b#\n"
		"#.#.#.#.#.#\n"
		"#^#^#^#^#^#\n"
		"#.#.#.#.#.#\n"
		"#....@....#\n"
		"###########\n");
}

void build_level_slime_test(Game* game, Log* log)
{
	from_string(game,
		"##############################\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#.............s..............#\n"
		"#............................#\n"
		"#........s........s..........#\n"
		"#............................#\n"
		"#............................#\n"
		"#......s......@......s.......#\n"
		"#............................#\n"
		"#............................#\n"
		"#........s.........s.........#\n"
		"#.............s..............#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"##############################\n");
}

void build_level_lich_test(Game* game, Log* log)
{
	from_string(game,
		"##############################\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#.............L..............#\n"
		"#............................#\n"
		"#........S........S..........#\n"
		"#............................#\n"
		"#............................#\n"
		"#......S......@......S.......#\n"
		"#............................#\n"
		"#............................#\n"
		"#........S.........S.........#\n"
		"#.............S..............#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"##############################\n");
}

void build_level_field_of_vision_test(Game* game, Log* log)
{
	from_string(game,
		"##############################\n"
		"#............................#\n"
		"#............................#\n"
		"#.................#.#.#......#\n"
		"#............................#\n"
		"#.............#...#...#...#..#\n"
		"#............................#\n"
		"#........#....#...#.#.#.#.#..#\n"
		"#..................#.........#\n"
		"#................#..#........#\n"
		"#......#......@......#.......#\n"
		"#............................#\n"
		"#............................#\n"
		"#........#.........#.........#\n"
		"#.............#.....#..#.....#\n"
		"#............................#\n"
		"#................#...#.#.....#\n"
		"#............................#\n"
		"#................#..#........#\n"
		"#............................#\n"
		"#............................#\n"
		"##############################\n");
}

// =============================================================================
// XXX

Build_Level_Func_Metadata BUILD_LEVEL_FUNCS[] = {
#define FUNC(name) { "build_level_" #name, build_level_##name },
	LEVEL_GEN_FUNCS
#undef FUNC
};
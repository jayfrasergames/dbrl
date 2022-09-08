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
// set bits represent walls
// unset bits are clear
// the border bits are guaranteed to be walls

static bool is_tile_wall_or_empty(Tile t)
{
	return (t.type == TILE_EMPTY) || (t.type == TILE_WALL);
}

static void remove_internal_walls(Map_Cache<Tile>* m)
{
	auto &map = *m;
	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			Pos p = Pos(x, y);
			if (!is_tile_wall_or_empty(map[Pos(x - 1, y - 1)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(    x, y - 1)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(x + 1, y - 1)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(x - 1,     y)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(    x,     y)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(x + 1,     y)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(x - 1, y + 1)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(    x, y + 1)])) { continue; }
			if (!is_tile_wall_or_empty(map[Pos(x + 1, y + 1)])) { continue; }
			map[p] = {};
		}
	}

	// TODO -- edges!!!
}

static void move_to_top_left(Map_Cache_Bool* map, v2_u32* size)
{
	v2_u32 orig_size = *size;
	v2_u32 offset = v2_u32(256, 256);
	v2_u32 bound = v2_u32(0, 0);
	for (u32 y = 0; y < orig_size.h; ++y) {
		for (u32 x = 0; x < orig_size.w; ++x) {
			Pos p = Pos(x, y);
			if (!map->get(p)) {
				offset = jfg_min(offset, (v2_u32)p);
				bound = jfg_max(bound, (v2_u32)p);
			}
		}
	}
	offset -= v2_u32(1, 1);
	bound += v2_u32(2, 2);

	v2_u32 new_size = bound - offset;
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

	*size = new_size;
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
	v2_u32 size = *out_size;

	f32 area = 0.0f;
	f32 best_area = 0.0f;
	Map_Cache_Bool best_map = {};
	log("================================================================================");
	do {
		Map_Cache_Bool tmp1 = {};
		Map_Cache_Bool tmp2 = {};
		Map_Cache_Bool *front = &tmp1;
		Map_Cache_Bool *back = &tmp2;
		front->reset();
		back->reset();
		for (u32 x = 0; x < size.w; ++x) {
			front->set(Pos(x,          0));
			front->set(Pos(x, size.h - 1));
		}
		for (u32 y = 0; y < size.h; ++y) {
			front->set(Pos(         0, y));
			front->set(Pos(size.w - 1, y));
		}

		for (u32 y = 1; y < size.h - 1; ++y) {
			for (u32 x = 1; x < size.w - 1; ++x) {
				if (rand_f32() > wall_chance) {
					front->set(Pos(x, y));
				}
			}
		}

		memcpy(back, front, sizeof(*back));

		for (u32 i = 0; i < num_iters; ++i) {
			log("--------------------------------------------------------------------------------");
			log_map(front, size);
			for (u32 y = 1; y < size.h - 1; ++y) {
				for (u32 x = 1; x < size.w - 1; ++x) {
					u32 wall_count = 0;
					if (front->get(Pos(x - 1, y - 1))) { ++wall_count; }
					if (front->get(Pos(    x, y - 1))) { ++wall_count; }
					if (front->get(Pos(x + 1, y - 1))) { ++wall_count; }
					if (front->get(Pos(x - 1,     y))) { ++wall_count; }
					if (front->get(Pos(x + 1,     y))) { ++wall_count; }
					if (front->get(Pos(x - 1, y + 1))) { ++wall_count; }
					if (front->get(Pos(    x, y + 1))) { ++wall_count; }
					if (front->get(Pos(x + 1, y + 1))) { ++wall_count; }

					if (front->get(Pos(x, y))) {
						if (wall_count < remain_wall_count) {
							back->unset(Pos(x, y));
						} else {
							back->set(Pos(x, y));
						}
					} else {
						if (wall_count >= become_wall_count) {
							back->set(Pos(x, y));
						} else {
							back->unset(Pos(x, y));
						}
					}
				}
			}
			auto tmp = front;
			front = back;
			back = tmp;
		}
		log("--------------------------------------------------------------------------------");
		log_map(front, size);

		biggest_connected_component(front, size);

		f32 num_floors = 0.0f;
		for (u32 y = 1; y < size.h - 1; ++y) {
			for (u32 x = 1; x < size.w - 1; ++x) {
				if (!front->get(Pos(x, y))) {
					++num_floors;
				}
			}
		}
		area = num_floors / (f32)(size.w * size.h);
		logf("attempt %u, area=%f, min_area=%f", max_attempts, area, minimum_area);
		if (area > best_area) {
			best_area = area;
			memcpy(&best_map, front, sizeof(best_map));
		}
	} while (area < minimum_area && --max_attempts);

	memcpy(map, &best_map, sizeof(*map));

	move_to_top_left(map, out_size);
}

static const u32 ITEM_TAKES_UP_TILE = 0x100;

enum Item_Type
{
	ITEM_NONE,
	ITEM_SPIDERWEB,
	ITEM_SPIDER_TRAP,

	ITEM_SPIDER_NORMAL = ITEM_TAKES_UP_TILE,
	ITEM_SPIDER_WEB,
	ITEM_SPIDER_POISON,
	ITEM_SPIDER_SHADOW,
};

struct Item
{
	Item_Type type;
	Pos       pos;
	union {
		struct {
			u32 radius;
		} spider_trap;
	};
};

// struct Room
// How can I store the items for the room?
#define ROOM_MAX_ITEMS 64

struct Room
{
	v2_u32                                 size;
	Map_Cache<Tile>                        tiles;
	Max_Length_Array<Item, ROOM_MAX_ITEMS> items;
};

static Pos random_floor_pos(Room* room)
{
	Pos result = {};
	u32 floor_tiles_seen = 0;
	for (u32 y = 0; y < room->size.h; ++y) {
		for (u32 x = 0; x < room->size.w; ++x) {
			Pos p = Pos(x, y);
			if (room->tiles[p].type == TILE_FLOOR) {
				for (u32 i = 0; i < room->items.len; ++i) {
					if (room->items[i].pos == p && room->items[i].type & ITEM_TAKES_UP_TILE) {
						goto next_tile;
					}
				}
				if (rand_u32() % ++floor_tiles_seen == 0) {
					result = p;
				}
			}
		next_tile: ;
		}
	}
	return result;
}

static void make_spider_room(Room* room)
{
	Map_Cache_Bool map = {};
	cellular_automata(&map, &room->size, 0.5f, 4, 5, 4, 0.4f, 20);

	f32 corner_web_chance = 0.5f;
	f32 centre_web_chance = 0.125f;

	for (u32 y = 0; y < room->size.h; ++y) {
		for (u32 x = 0; x < room->size.w; ++x) {
			Pos p = Pos(x, y);
			Tile t = {};
			if (map.get(p)) {
				t.type = TILE_WALL;
				t.appearance = APPEARANCE_WALL_BROWN_ROCK;
			} else {
				t.type = TILE_FLOOR;
				t.appearance = APPEARANCE_FLOOR_ROCK;
			}
			room->tiles[p] = t;
		}
	}

	for (u32 y = 1; y < room->size.h - 1; ++y) {
		for (u32 x = 1; x < room->size.w - 1; ++x) {
			if (map.get(Pos(x, y))) {
				continue;
			}

			u64 top    = map.get(Pos(x, y - 1));
			u64 bottom = map.get(Pos(x, y + 1));
			u64 left   = map.get(Pos(x - 1, y));
			u64 right  = map.get(Pos(x + 1, y));

			Item web = {};
			web.type = ITEM_SPIDERWEB;
			web.pos = Pos(x, y);
			u32 wall_count = 0;
			if (top)    { ++wall_count; }
			if (bottom) { ++wall_count; }
			if (left)   { ++wall_count; }
			if (right)  { ++wall_count; }
			f32 chance = wall_count >= 2 ? corner_web_chance : centre_web_chance;

			if (rand_f32() < chance) {
				room->items.append(web);
			}
		}
	}

	Item spider = {};

	spider.type = ITEM_SPIDER_NORMAL;
	spider.pos = random_floor_pos(room);
	room->items.append(spider);

	spider.pos = random_floor_pos(room);
	room->items.append(spider);

	spider.type = ITEM_SPIDER_WEB;
	spider.pos = random_floor_pos(room);
	room->items.append(spider);

	spider.type = ITEM_SPIDER_POISON;
	spider.pos = random_floor_pos(room);
	room->items.append(spider);

	spider.type = ITEM_SPIDER_SHADOW;
	spider.pos = random_floor_pos(room);
	room->items.append(spider);

	Pos center = (Pos)(room->size / (u32)2);
	u32 radius = min_u32(room->size.w, room->size.h);

	Item trap = {};
	trap.type = ITEM_SPIDER_TRAP;
	trap.pos = center;
	trap.spider_trap.radius = radius;
	room->items.append(trap);
}

static void draw_room(Game* game, Room* room, v2_u32 offset)
{
	v2_u32 size = room->size;
	for (u32 y = 0; y < size.h; ++y) {
		for (u32 x = 0; x < size.w; ++x) {
			Pos room_pos = Pos(x, y);
			Pos game_pos = room_pos + (Pos)offset;
			game->tiles[game_pos] = room->tiles[room_pos];
		}
	}

	for (u32 i = 0; i < room->items.len; ++i) {
		auto item = room->items[i];
		Pos p = item.pos + (Pos)offset;
		switch (item.type) {
		case ITEM_NONE:
			ASSERT(0);
			break;
		case ITEM_SPIDERWEB:
			add_spiderweb(game, p);
			break;
		case ITEM_SPIDER_NORMAL:
			add_spider_normal(game, p);
			break;
		case ITEM_SPIDER_WEB:
			add_spider_web(game, p);
			break;
		case ITEM_SPIDER_POISON:
			add_spider_poison(game, p);
			break;
		case ITEM_SPIDER_SHADOW:
			add_spider_shadow(game, p);
			break;
		case ITEM_SPIDER_TRAP:
			add_trap_spider_cave(game, p, item.spider_trap.radius);
			break;
		default:
			ASSERT(0);
			break;
		}
	}
}

static void random_place_player(Game* game)
{
	u32 floors_seen = 0;
	Pos player_pos;
	for (u32 y = 0; y < 256; ++y) {
		for (u32 x = 0; x < 256; ++x) {
			Pos p = Pos(x, y);
			if (game->tiles[p].type == TILE_FLOOR) {
				if (rand_u32() % ++floors_seen == 0) {
					player_pos = p;
				}
			}
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
	room.size = v2_u32(20, 20);
	make_spider_room(&room);

	draw_room(game, &room, v2_u32(10, 10));
	remove_internal_walls(&game->tiles);
	random_place_player(game);

	update_fov(game);
}

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
			add_spiderweb(game, cur_pos);

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
			e->default_action = ACTION_BUMP_ATTACK;

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
			e->default_action = ACTION_BUMP_ATTACK;

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
			e->default_action = ACTION_BUMP_ATTACK;

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
			// e->default_action = ACTION_BUMP_ATTACK;

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
			e->flags = ENTITY_FLAG_BLOCKS_VISION;
			e->default_action = ACTION_OPEN_DOOR;

			break;
		}
		case 'i': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			add_imp(game, cur_pos);
		}

		case 'o': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			add_explosive_barrel(game, cur_pos);
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
		"#.........b...#b#..##....................#\n"
		"#......#.bwb..#b#...............o........#\n"
		"#.....###.b...#b#.......###...o....o.....#\n"
		"#....#####....#b#.......#x#.....o.....o..#\n"
		"#...##.#.##...#b#.....###x#.#......o.o...#\n"
		"#..##..#..##..#b#.....#xxxxx#....o....o..#\n"
		"#...b..#......#b#.....###x###.o....o.....#\n"
		"#b.bwb.#......#w#.......#x#.....o..o..o..#\n"
		"#wb.b..#...b..@.+.......###..............#\n"
		"#b.....#..bwb.###........................#\n"
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

void build_level_imp_test(Game* game, Log* log)
{
	from_string(game,
		"###############\n"
		"#.............#\n"
		"#.............#\n"
		"#.............#\n"
		"#.....i.......#\n"
		"#.............#\n"
		"#.............#\n"
		"#.....@.......#\n"
		"#.............#\n"
		"#.............#\n"
		"###############\n");
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
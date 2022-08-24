#include "ui.h"

#include "stdafx.h"
#include "constants.h"
#include "random.h"

#include "draw.h"

#define JFG_HEADER_ONLY
#include "gen/boxy_bold.data.h"
#undef JFG_HEADER_ONLY

static f32 angle_to_sprite_x_coord(f32 angle)
{
	angle /= PI_F32;
	f32 x;
	if      (angle < -0.875f) { x = 2.0f; }
	else if (angle < -0.625f) { x = 5.0f; }
	else if (angle < -0.375f) { x = 3.0f; }
	else if (angle < -0.125f) { x = 4.0f; }
	else if (angle <  0.125f) { x = 0.0f; }
	else if (angle <  0.375f) { x = 7.0f; }
	else if (angle <  0.625f) { x = 1.0f; }
	else if (angle <  0.875f) { x = 6.0f; }
	else                      { x = 2.0f; }
	return x;
}

void world_anim_init(Anim_State* anim_state, Game* game)
{
	u32 num_entities = game->entities.len;

	auto &world_static_anims = anim_state->world_static_anims;

	for (u32 i = 0; i < num_entities; ++i) {
		auto e = &game->entities[i];
		auto app = e->appearance;
		if (!app) {
			continue;
		}
		Pos pos = e->pos;
		if (appearance_is_creature(app)) {
			World_Static_Anim ca = {};
			ca.type = ANIM_CREATURE_IDLE;
			ca.sprite_coords = appearance_get_creature_sprite_coords(app);
			ca.world_coords = (v2)pos;
			ca.entity_id = e->id;
			ca.depth_offset = constants.z_offsets.character;
			ca.idle.duration = 0.8f + 0.4f * rand_f32();
			ca.idle.offset = 0.0f;
			world_static_anims.append(ca);

		} else if (appearance_is_floor(app)) {
			World_Static_Anim ta = {};
			ta.type = ANIM_TILE_STATIC;
			ta.sprite_coords = appearance_get_floor_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = constants.z_offsets.floor;
			world_static_anims.append(ta);

		} else if (appearance_is_liquid(app)) {
			World_Static_Anim ta = {};
			ta.type = ANIM_TILE_LIQUID;
			ta.sprite_coords = appearance_get_liquid_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = constants.z_offsets.floor;

			f32 d = uniform_f32(1.2f, 1.8f);
			ta.tile_liquid.offset = uniform_f32(0.0f, d);
			ta.tile_liquid.duration = d;
			ta.tile_liquid.second_sprite_coords = ta.sprite_coords + v2(0.0f, 1.0f);

			world_static_anims.append(ta);

		} else if (appearance_is_item(app)) {
			World_Static_Anim ia = {};
			ia.type = ANIM_TILE_STATIC;
			ia.sprite_coords = appearance_get_item_sprite_coords(app);
			ia.world_coords = (v2)pos;
			ia.entity_id = e->id;
			ia.depth_offset = constants.z_offsets.item;
			world_static_anims.append(ia);

		} else if (appearance_is_door(app)) {
			World_Static_Anim da = {};
			da.type = ANIM_TILE_STATIC;
			da.sprite_coords = appearance_get_door_sprite_coords(app);
			da.world_coords = (v2)pos;
			da.entity_id = e->id;
			da.depth_offset = constants.z_offsets.door;
			world_static_anims.append(da);
		}
	}

	auto& tiles = game->tiles;
	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			Pos p = Pos(x, y);
			Tile c = tiles[p];
			switch (c.type) {
			case TILE_EMPTY:
				break;
			case TILE_FLOOR: {
				v2 sprite_coords = appearance_get_floor_sprite_coords(c.appearance);
				World_Static_Anim anim = {};
				anim.type = ANIM_TILE_STATIC;
				anim.sprite_coords = sprite_coords;
				anim.world_coords = (v2)p;
				anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
				anim.depth_offset = constants.z_offsets.floor;
				world_static_anims.append(anim);
				break;
			}
			case TILE_WALL: {
				Appearance app = c.appearance;
				u8 tl = tiles[(Pos)((v2_i16)p + v2_i16(-1, -1))].appearance == app;
				u8 t  = tiles[(Pos)((v2_i16)p + v2_i16( 0, -1))].appearance == app;
				u8 tr = tiles[(Pos)((v2_i16)p + v2_i16( 1, -1))].appearance == app;
				u8 l  = tiles[(Pos)((v2_i16)p + v2_i16(-1,  0))].appearance == app;
				u8 r  = tiles[(Pos)((v2_i16)p + v2_i16( 1,  0))].appearance == app;
				u8 bl = tiles[(Pos)((v2_i16)p + v2_i16(-1,  1))].appearance == app;
				u8 b  = tiles[(Pos)((v2_i16)p + v2_i16( 0,  1))].appearance == app;
				u8 br = tiles[(Pos)((v2_i16)p + v2_i16( 1,  1))].appearance == app;

				u8 connection_mask = 0;
				if (t && !(tl && tr && l && r)) {
					connection_mask |= APPEARANCE_N;
				}
				if (r && !(t && tr && b && br)) {
					connection_mask |= APPEARANCE_E;
				}
				if (b && !(l && r && bl && br)) {
					connection_mask |= APPEARANCE_S;
				}
				if (l && !(t && b && tl && bl)) {
					connection_mask |= APPEARANCE_W;
				}

				World_Static_Anim anim = {};
				anim.type = ANIM_TILE_STATIC;
				anim.sprite_coords = appearance_get_wall_sprite_coords(app, connection_mask);
				anim.world_coords = (v2)p;
				anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
				anim.depth_offset = constants.z_offsets.wall;
				world_static_anims.append(anim);

				if (tiles[(Pos)((v2_i16)p + v2_i16( 0,  1))].type != TILE_WALL) {
					anim.sprite_coords = { 30.0f, 36.0f };
					anim.world_coords = (v2)p + v2(0.0f, 1.0f);
					anim.depth_offset = constants.z_offsets.wall_shadow;
					world_static_anims.append(anim);
				}

				break;
			}
			case TILE_WATER: {
				v2 sprite_coords = appearance_get_liquid_sprite_coords(c.appearance);

				World_Static_Anim anim = {};
				anim.type = ANIM_TILE_LIQUID;
				anim.sprite_coords = sprite_coords;
				anim.world_coords = (v2)p;
				anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
				anim.depth_offset = constants.z_offsets.floor;

				f32 d = uniform_f32(0.8f, 1.2f);
				anim.tile_liquid.offset = uniform_f32(0.0f, d);
				anim.tile_liquid.duration = d;
				anim.tile_liquid.second_sprite_coords = sprite_coords + v2(0.0f, 1.0f);

				world_static_anims.append(anim);


				Tile_Type t = c.type;
				u8 mask = 0;
				if (tiles[(Pos)((v2_i16)p + v2_i16( 0, -1))].type == t) { mask |= 0x01; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 1, -1))].type == t) { mask |= 0x02; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 1,  0))].type == t) { mask |= 0x04; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 1,  1))].type == t) { mask |= 0x08; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 0,  1))].type == t) { mask |= 0x10; }
				if (tiles[(Pos)((v2_i16)p + v2_i16(-1,  1))].type == t) { mask |= 0x20; }
				if (tiles[(Pos)((v2_i16)p + v2_i16(-1,  0))].type == t) { mask |= 0x40; }
				if (tiles[(Pos)((v2_i16)p + v2_i16(-1, -1))].type == t) { mask |= 0x80; }

				if (~mask & 0xFF) {
					World_Static_Anim anim = {};
					anim.type = ANIM_WATER_EDGE;
					anim.sprite_coords = { (f32)(mask % 16), (f32)(15 - mask / 16) };
					anim.world_coords = (v2)p;
					anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
					anim.depth_offset = constants.z_offsets.water_edge;
					anim.water_edge.color = { 0x58, 0x80, 0xC0, 0xFF };
					world_static_anims.append(anim);
				}
			}
			}

		}
	}

	// add field of vision
	{
		World_Anim_Dynamic a = {};
		a.type = ANIM_FIELD_OF_VISION_CHANGED;
		a.start_time = 0.0f;
		a.duration = constants.anims.move.duration;
		a.field_of_vision.buffer_id = 0;
		// a.field_of_vision.fov = &game->field_of_vision;
		anim_state->world_dynamic_anims.append(a);
	}
}


void init(Anim_State* anim_state, Game* game)
{
	anim_state->dyn_time_start = 0.0f;
	anim_state->camera_offset = v2(0.0f, 0.0f);

	anim_state->world_static_anims.reset();
	anim_state->world_dynamic_anims.reset();
	// anim_state->world_modifier_anims.reset();
	anim_state->sound_anims.reset();

	world_anim_init(anim_state, game);
}

void build_animations(Anim_State* anim_state, Slice<Event> events, f32 time)
{
	// XXX -- need to get rid of this, make a proper particle system
	anim_state->draw->renderer.particles.particles.reset();
	anim_state->dyn_time_start = time;

	// auto &world_static_anims =  anim_state->world_static_anims;
	auto &world_dynamic_anims = anim_state->world_dynamic_anims;
	// auto &world_anim_mods = anim_state->world_modifier_anims;
	auto &sound_anims = anim_state->sound_anims;
	auto &card_anims = anim_state->card_anim_state.card_anims;
	auto &card_anim_modifiers = anim_state->card_anim_state.card_anim_modifiers;

	for (u32 i = 0; i < events.len; ++i) {
		Event *event = &events[i];
		switch (event->type) {
		case EVENT_MOVE: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_MOVE;
			anim.start_time = event->time;
			anim.duration = constants.anims.move.duration;
			anim.move.entity_id = event->move.entity_id;
			anim.move.start = (v2)event->move.start;
			anim.move.end   = (v2)event->move.end;
			world_dynamic_anims.append(anim);
			break;
		}
		case EVENT_MOVE_BLOCKED: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_MOVE_BLOCKED;
			anim.start_time = event->time;
			anim.duration = constants.anims.move.duration;
			anim.move.entity_id = event->move.entity_id;
			anim.move.start = (v2)event->move.start;
			anim.move.end   = (v2)event->move.end;
			world_dynamic_anims.append(anim);
			break;
		}
		case EVENT_OPEN_DOOR: {
			World_Anim_Dynamic anim = {};
			anim.start_time = event->time;
			anim.duration = 0.0f;
			anim.open_door.entity_id = event->open_door.door_id;
			anim.open_door.spirte_coords = appearance_get_item_sprite_coords(event->open_door.new_appearance);
			world_dynamic_anims.append(anim);

			Sound_Anim open_sound = {};
			open_sound.start_time = event->time;
			open_sound.sound_id = SOUND_FANTASY_GAME_DOOR_OPEN;
			sound_anims.append(open_sound);
			break;
		}
		case EVENT_BUMP_ATTACK: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_MOVE_BLOCKED;
			anim.start_time = event->time;
			anim.duration = constants.anims.bump_attack.duration;
			anim.move.entity_id = event->bump_attack.attacker_id;
			anim.move.start = (v2)event->bump_attack.start;
			anim.move.end   = (v2)event->bump_attack.end;
			world_dynamic_anims.append(anim);

			Sound_Anim bump_sound = {};
			bump_sound.start_time = event->time;
			bump_sound.sound_id = event->bump_attack.sound;
			sound_anims.append(bump_sound);
			break;
		}

		case EVENT_CREATURE_DROP_IN: {
			f32 anim_time = event->time - constants.anims.creature_drop_in.duration;

			World_Anim_Dynamic anim = {};
			anim.type = ANIM_CREATURE_DROP_IN;
			anim.start_time = anim_time;
			anim.duration = constants.anims.creature_drop_in.duration;
			anim.creature_drop_in.entity_id = event->creature_drop_in.entity_id;
			anim.creature_drop_in.sprite_coords = appearance_get_creature_sprite_coords(event->creature_drop_in.appearance);
			anim.creature_drop_in.world_coords = (v2)event->creature_drop_in.pos;
			world_dynamic_anims.append(anim);

			Sound_Anim sound = {};
			sound.start_time = anim_time;
			sound.sound_id = SOUND_SPIDER_RUNNING_01_LOOP;
			sound_anims.append(sound);
			break;
		}

		case EVENT_DROP_TILE: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_DROP_TILE;
			anim.start_time = event->time;
			anim.duration = constants.anims.drop_tile.duration;
			anim.drop_tile.entity_id = MAX_ENTITIES + pos_to_u16(event->drop_tile.pos);
			world_dynamic_anims.append(anim);
			break;
		}
		case EVENT_FIREBALL_SHOT: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_PROJECTILE_EFFECT_32;
			anim.start_time = event->time;
			anim.duration = constants.anims.fireball.shot_duration;
			anim.projectile.start = (v2)event->fireball_shot.start;
			anim.projectile.end = (v2)event->fireball_shot.end;
			world_dynamic_anims.append(anim);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_FIRE_SPELL_02;
			sound_anims.append(sound);
			break;
		}
		case EVENT_FIREBALL_HIT: {
			World_Anim_Dynamic shake = {};
			shake.type = ANIM_CAMERA_SHAKE;
			shake.start_time = event->time;
			shake.duration = constants.anims.fireball.shake_duration;
			shake.camera_shake.power = constants.anims.fireball.shake_power;
			world_dynamic_anims.append(shake);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_FIRE_SPELL_13;
			sound_anims.append(sound);
			break;
		}
		case EVENT_FIREBALL_OFFSHOOT_2: {
			World_Anim_Dynamic a = {};
			a.type = ANIM_PROJECTILE_EFFECT_24;
			a.start_time = event->time;
			a.duration = event->fireball_offshoot_2.duration;
			a.projectile.start = event->fireball_offshoot_2.start;
			a.projectile.end = event->fireball_offshoot_2.end;
			v2 dir = a.projectile.end - a.projectile.start;
			f32 angle = atan2f(-dir.y, dir.x);
			a.projectile.sprite_coords = v2(angle_to_sprite_x_coord(angle), 11.0f);
			world_dynamic_anims.append(a);
			break;
		}
		case EVENT_LIGHTNING_BOLT: {
			World_Anim_Dynamic a = {};
			a.type = ANIM_PROJECTILE_EFFECT_24;
			a.start_time = event->time;
			a.duration = event->lightning_bolt.duration;
			a.projectile.start = event->lightning_bolt.start;
			a.projectile.end = event->lightning_bolt.end;
			a.projectile.sprite_coords = v2(0.0f, 9.0f);
			world_dynamic_anims.append(a);
			break;
		}
		case EVENT_LIGHTNING_BOLT_START: {
			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_LIGHTNING_SPELL_03;
			sound_anims.append(sound);
			break;
		}
		case EVENT_STUCK: {
			World_Anim_Dynamic text_anim = {};
			text_anim.type = ANIM_TEXT;
			text_anim.start_time = event->time;
			text_anim.duration = constants.anims.text_duration;
			text_anim.text.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
			text_anim.text.caption[0] = '*';
			text_anim.text.caption[1] = 's';
			text_anim.text.caption[2] = 't';
			text_anim.text.caption[3] = 'u';
			text_anim.text.caption[4] = 'c';
			text_anim.text.caption[5] = 'k';
			text_anim.text.caption[6] = '*';
			text_anim.text.caption[7] = 0;
			text_anim.text.pos = (v2)event->stuck.pos;
			world_dynamic_anims.append(text_anim);
			break;
		}
		case EVENT_DAMAGED: {
			World_Anim_Dynamic text_anim = {};
			text_anim.type = ANIM_TEXT;
			text_anim.start_time = event->time;
			text_anim.duration = constants.anims.text_duration;
			text_anim.text.color = v4(1.0f, 0.0f, 0.0f, 1.0f);

			char *buffer = fmt("-%u", event->damaged.amount);
			u32 i = 0;
			for (char *p = buffer; *p; ++p, ++i) {
				text_anim.text.caption[i] = (u8)*p;
			}
			ASSERT(i < ARRAY_SIZE(text_anim.text.caption));
			text_anim.text.caption[i] = 0;
			text_anim.text.pos = event->damaged.pos;
			world_dynamic_anims.append(text_anim);
			break;
		}
		case EVENT_POISONED: {
			World_Anim_Dynamic text_anim = {};
			text_anim.type = ANIM_TEXT;
			text_anim.start_time = event->time;
			text_anim.duration = constants.anims.text_duration;
			text_anim.text.color = v4(0.0f, 1.0f, 0.0f, 1.0f);
			for (char *p = "*poisoned*", *q = (char*)text_anim.text.caption; *p; ++p, ++q) {
				*q = *p;
			}
			text_anim.text.pos = event->poisoned.pos;
			world_dynamic_anims.append(text_anim);
			break;
		}
		case EVENT_DEATH: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_DEATH;
			anim.start_time = event->time;
			anim.duration = 0.0f;
			anim.death.entity_id = event->death.entity_id;
			world_dynamic_anims.append(anim);
			break;
		}
		case EVENT_EXCHANGE: {
			// XXX -- hack
			f32 start_time = event->time - constants.anims.exchange.particle_start;
			f32 duration = constants.anims.exchange.particle_duration;

			World_Anim_Dynamic ex = {};
			ex.type = ANIM_EXCHANGE;
			ex.start_time = start_time;
			ex.duration = duration;
			ex.exchange.a = event->exchange.a;
			ex.exchange.b = event->exchange.b;
			world_dynamic_anims.append(ex);

			World_Anim_Dynamic particles = {};
			particles.type = ANIM_EXCHANGE_PARTICLES;
			particles.start_time = start_time;
			particles.duration = duration;
			particles.exchange_particles.pos = (v2)event->exchange.a_pos;
			world_dynamic_anims.append(particles);

			particles.exchange_particles.pos = (v2)event->exchange.b_pos;
			world_dynamic_anims.append(particles);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_CAST_03;
			sound_anims.append(sound);
			break;
		}
		case EVENT_BLINK: {
			// TODO -- spell casting sounds/anims/particle effect stuff
			f32 start_time = event->time - constants.anims.blink.particle_start;
			f32 duration = constants.anims.blink.particle_duration;

			World_Anim_Dynamic anim = {};
			anim.type = ANIM_BLINK;
			anim.start_time = start_time;
			anim.duration = constants.anims.blink.particle_duration;
			anim.blink.entity_id = event->blink.caster_id;
			anim.blink.target = (v2)event->blink.target;
			world_dynamic_anims.append(anim);

			anim.type = ANIM_BLINK_PARTICLES;
			anim.blink_particles.pos = (v2)event->blink.start;
			world_dynamic_anims.append(anim);

			anim.type = ANIM_BLINK_PARTICLES;
			anim.blink_particles.pos = (v2)event->blink.target;
			world_dynamic_anims.append(anim);

			Sound_Anim sound = {};
			sound.start_time = start_time;
			sound.sound_id = SOUND_CARD_GAME_ABILITIES_POOF_02;
			sound_anims.append(sound);
			break;
		}
		case EVENT_SLIME_SPLIT: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_SLIME_SPLIT;
			anim.start_time = event->time;
			anim.duration = constants.anims.slime_split.duration;
			anim.slime_split.original_id = event->slime_split.original_id;
			anim.slime_split.new_id = event->slime_split.new_id;
			anim.slime_split.start = event->slime_split.start;
			anim.slime_split.end = event->slime_split.end;
			anim.slime_split.sprite_coords = appearance_get_creature_sprite_coords(APPEARANCE_CREATURE_GREEN_SLIME);
			world_dynamic_anims.append(anim);
			break;
		}
		case EVENT_SHOOT_WEB_CAST: {
			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_SHADOW_ATTACK_4;
			sound_anims.append(sound);

			World_Anim_Dynamic particles = {};
			particles.type = ANIM_SHOOT_WEB_PARTICLES;
			particles.start_time = event->time;
			particles.duration = constants.anims.shoot_web.shot_duration;
			particles.shoot_web_particles.start = event->shoot_web_cast.start;
			particles.shoot_web_particles.end = event->shoot_web_cast.end;
			world_dynamic_anims.append(particles);
			break;
		}
		case EVENT_SHOOT_WEB_HIT: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_ADD_ITEM;
			anim.start_time = event->time;
			anim.duration = 0.0f;
			anim.add_item.sprite_coords = appearance_get_item_sprite_coords(event->shoot_web_hit.appearance);
			anim.add_item.pos = event->shoot_web_hit.pos;
			anim.add_item.entity_id = event->shoot_web_hit.web_id;
			world_dynamic_anims.append(anim);
			break;
		}
		case EVENT_FIRE_BOLT_SHOT: {
			World_Anim_Dynamic a = {};
			a.type = ANIM_PROJECTILE_EFFECT_32;
			a.start_time = event->time;
			a.duration = event->fire_bolt_shot.duration;
			v2 start = event->fire_bolt_shot.start;
			v2 end = event->fire_bolt_shot.end;
			a.projectile.start = start;
			a.projectile.end = end;
			v2 dir = end - start;
			f32 angle = atan2f(-dir.y, dir.x);
			a.projectile.sprite_coords = v2(angle_to_sprite_x_coord(angle), 5.0f);
			world_dynamic_anims.append(a);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_FIRE_SPELL_04;
			sound_anims.append(sound);
			break;
		}
		case EVENT_POLYMORPH: {
			f32 start_time = event->time;
			f32 duration = constants.anims.polymorph.duration;

			World_Anim_Dynamic anim = {};
			anim.type = ANIM_POLYMORPH;
			anim.start_time = start_time + duration / 2.0f;
			anim.duration = 0.0f;
			anim.polymorph.entity_id = event->polymorph.entity_id;
			anim.polymorph.new_sprite_coords = appearance_get_creature_sprite_coords(event->polymorph.new_appearance);
			world_dynamic_anims.append(anim);

			anim = {};
			anim.type = ANIM_POLYMORPH_PARTICLES;
			anim.start_time = event->time;
			anim.duration = 0.0f;
			anim.polymorph_particles.pos = (v2)event->polymorph.pos;
			world_dynamic_anims.append(anim);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_SHADOW_SPELL_01;
			sound_anims.append(sound);
		}
		case EVENT_HEAL: {
			World_Anim_Dynamic a = {};
			a.type = ANIM_HEAL_PARTICLES;
			a.start_time = event->time;
			a.duration = constants.anims.heal.cast_time;
			a.heal_particles.start = (v2)event->heal.start;
			a.heal_particles.end = (v2)event->heal.end;
			world_dynamic_anims.append(a);

			a = {};
			a.type = ANIM_TEXT;
			a.start_time = event->time + constants.anims.heal.cast_time;
			a.duration = 1.0f;
			a.text.color = v4(0.0f, 1.0f, 1.0f, 1.0f);
			snprintf((char*)a.text.caption, ARRAY_SIZE(a.text.caption), "%d", event->heal.amount);
			a.text.pos = (v2)event->heal.end;
			world_dynamic_anims.append(a);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_HEALING_01;
			sound_anims.append(sound);
			break;
		}
		case EVENT_FIELD_OF_VISION_CHANGED: {
			World_Anim_Dynamic a = {};
			a.type = ANIM_FIELD_OF_VISION_CHANGED;
			a.start_time = event->time;
			a.duration = event->field_of_vision.duration;
			// a.field_of_vision.fov = event->field_of_vision.fov;
			world_dynamic_anims.append(a);
			break;
		}
		case EVENT_ADD_CREATURE: {
			World_Anim_Dynamic a = {};
			a.type = ANIM_ADD_CREATURE;
			a.start_time = event->time;
			a.duration = 0.0f;
			a.add_creature.entity_id = event->add_creature.creature_id;
			a.add_creature.pos = event->add_creature.pos;
			a.add_creature.sprite_coords = appearance_get_creature_sprite_coords(event->add_creature.appearance);
			world_dynamic_anims.append(a);
			break;
		}
		case EVENT_ADD_CARD_TO_DISCARD: {
			Card_Anim anim = {};
			anim.type = CARD_ANIM_ADD_CARD_TO_DISCARD;

			anim.add_to_discard.start_time = event->time;
			anim.add_to_discard.duration = constants.cards_ui.add_to_discard_duration;

			anim.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
			anim.card_id = event->add_card_to_discard.card_id;
			anim.card_face = card_appearance_get_sprite_coords(event->add_card_to_discard.appearance);

			card_anims.append(anim);
			break;
		}
		case EVENT_CARD_POISON: {
			Card_Anim_Modifier anim = {};
			anim.type = CARD_ANIM_MOD_FLASH;
			anim.card_id = event->card_poison.card_id;
			anim.flash.color = v4(0.0f, 1.0f, 0.0f, 1.0f);
			anim.flash.start_time = event->time;
			anim.flash.duration = constants.anims.poison.anim_duration;
			anim.flash.flash_duration = constants.anims.poison.flash_duration;
			card_anim_modifiers.append(anim);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_NATURE_SPELL_10;
			sound_anims.append(sound);
			break;
		}
		case EVENT_TURN_INVISIBLE: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_TURN_INVISIBLE;
			anim.start_time = event->time;
			anim.duration = constants.anims.turn_visible.duration;
			anim.turn_visible.entity_id = event->turn_visible.entity_id;
			world_dynamic_anims.append(anim);

			Sound_Anim sound = {};
			sound.start_time = event->time;
			sound.sound_id = SOUND_CARD_GAME_MAGIC_INVISIBLE_01;
			sound_anims.append(sound);
			break;
		}
		case EVENT_TURN_VISIBLE: {
			World_Anim_Dynamic anim = {};
			anim.type = ANIM_TURN_VISIBLE;
			anim.start_time = event->time;
			anim.duration = constants.anims.turn_visible.duration;
			anim.turn_visible.entity_id = event->turn_visible.entity_id;
			world_dynamic_anims.append(anim);
			break;
		}

		}
	}
}

static World_Static_Anim* get_static_anim(Anim_State* anim_state, Entity_ID entity_id)
{
	auto &world_static_anims = anim_state->world_static_anims;
	for (u32 i = 0; i < world_static_anims.len; ++i) {
		auto anim = &world_static_anims[i];
		if (anim->entity_id == entity_id) {
			return anim;
		}
	}
	return NULL;
}

void draw(Anim_State* anim_state, Render* render, Sound_Player* sound_player, f32 time)
{
	auto r = &render->render_job_buffer;

	// XXX -- temporary
	// reset draw state
	auto draw = anim_state->draw;
	sprite_sheet_instances_reset(&draw->tiles);
	sprite_sheet_instances_reset(&draw->creatures);
	sprite_sheet_instances_reset(&draw->water_edges);
	sprite_sheet_instances_reset(&draw->effects_24);
	sprite_sheet_instances_reset(&draw->effects_32);
	sprite_sheet_font_instances_reset(&draw->boxy_bold);

	f32 dyn_time = time - anim_state->dyn_time_start;

	auto &world_static_anims = anim_state->world_static_anims;
	auto &world_dynamic_anims = anim_state->world_dynamic_anims;
	// auto &world_anim_mods = anim_state->world_modifier_anims;

	// start new dynamic animations
	for (u32 i = 0; i < world_dynamic_anims.len; ++i) {
		auto anim = &world_dynamic_anims[i];
		if (anim->start_time > dyn_time || anim->started) {
			continue;
		}
		anim->started = true;

		switch (anim->type) {
		case ANIM_MOVE:
		case ANIM_MOVE_BLOCKED: {
			auto static_anim = get_static_anim(anim_state, anim->move.entity_id);
			ASSERT(static_anim);

			World_Anim_Modifier modifier = {};
			modifier.type = ANIM_MOD_POS;
			modifier.pos.pos = v2(0.0f, 0.0f);
			static_anim->modifiers.append(modifier);
			break;
		}
		case ANIM_DROP_TILE: {
			auto static_anim = get_static_anim(anim_state, anim->drop_tile.entity_id);
			ASSERT(static_anim);

			World_Anim_Modifier modifier = {};
			modifier.type = ANIM_MOD_COLOR;
			modifier.color.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
			static_anim->modifiers.append(modifier);

			modifier = {};
			modifier.type = ANIM_MOD_POS;
			modifier.pos.pos = v2(0.0f, 0.0f);
			static_anim->modifiers.append(modifier);
			break;
		}
		case ANIM_TURN_INVISIBLE: {
			auto static_anim = get_static_anim(anim_state, anim->turn_invisible.entity_id);
			ASSERT(static_anim);

			World_Anim_Modifier modifier = {};
			modifier.type = ANIM_MOD_COLOR;
			modifier.color.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
			static_anim->modifiers.append(modifier);
			break;
		}
		case  ANIM_CREATURE_DROP_IN: { 
			World_Static_Anim static_anim = {};
			static_anim.type = ANIM_CREATURE_IDLE;
			static_anim.idle.duration = uniform_f32(0.8f, 1.2f);
			static_anim.idle.offset = rand_f32() * static_anim.idle.duration;
			static_anim.sprite_coords = anim->creature_drop_in.sprite_coords;
			static_anim.world_coords = anim->creature_drop_in.world_coords;
			static_anim.entity_id = anim->creature_drop_in.entity_id;

			World_Anim_Modifier modifier = {};
			modifier.type = ANIM_MOD_COLOR;
			modifier.color.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
			static_anim.modifiers.append(modifier);

			modifier = {};
			modifier.type = ANIM_MOD_POS;
			modifier.pos.pos = v2(0.0f, 0.0f);
			static_anim.modifiers.append(modifier);

			world_static_anims.append(static_anim);
			break;
		}
		case ANIM_SLIME_SPLIT: {
			World_Static_Anim new_anim = {};
			new_anim.type = ANIM_CREATURE_IDLE;
			new_anim.idle.offset = rand_f32();
			new_anim.idle.duration = uniform_f32(0.8f, 1.2f);
			new_anim.sprite_coords = anim->slime_split.sprite_coords;
			new_anim.world_coords = anim->slime_split.end;
			new_anim.entity_id = anim->slime_split.new_id;
			new_anim.depth_offset = constants.z_offsets.character;

			World_Anim_Modifier modifier = {};
			modifier.type = ANIM_MOD_POS;
			modifier.pos.pos = v2(0.0f, 0.0f);
			modifier.pos.y_offset = 0.0f;
			new_anim.modifiers.append(modifier);

			world_static_anims.append(new_anim);
			break;
		}

		case ANIM_BLINK_PARTICLES: {
			Particle_Instance instance = {};
			Particles *particles = &draw->renderer.particles;
			// instance.start_time = time;
			// instance.end_time = instance.start_time + constants.anims.blink.particle_duration;
			instance.start_time = anim->start_time;
			instance.end_time = anim->start_time + anim->duration;
			instance.start_pos = anim->blink_particles.pos;
			instance.start_color = { 0.0f, 1.0f, 1.0f, 1.0f };
			instance.end_color = { 1.0f, 1.0f, 1.0f, 1.0f };

			u32 num_particles = constants.anims.blink.num_particles;
			for (u32 i = 0; i < num_particles; ++i) {
				f32 speed = rand_f32() + 1.0f;
				f32 angle = 2.0f * PI_F32 * rand_f32();
				v2 v = { cosf(angle), sinf(angle) };
				instance.start_velocity = speed * v;
				particles_add(particles, instance);
			}
			break;
		}
		case ANIM_HEAL_PARTICLES: {
			u32 num_particles = constants.anims.heal.num_particles;

			Particles *particles = &draw->renderer.particles;
			Particle_Instance instance = {};
			v2 start = anim->heal_particles.start;
			v2 velocity = anim->heal_particles.end - start;
			f32 dist = sqrtf(velocity.x*velocity.x + velocity.y*velocity.y);
			velocity = velocity / constants.anims.heal.cast_time;
			instance.start_color = v4(1.0f, 1.0f, 1.0f, 1.0f);
			instance.end_color = v4(1.0f, 0.0f, 0.0f, 1.0f);
			// f32 start_time = time;
			// f32 end_time = time + constants.anims.heal.cast_time;
			f32 start_time = anim->start_time;
			f32 end_time = anim->start_time + constants.anims.heal.cast_time;

			for (u32 i = 0; i < num_particles; ++i) {
				f32 radius = uniform_f32(0.1f, 0.5f);
				f32 angle = rand_f32() * PI_F32 / 2.0f;
				v2 offset = radius * v2(cosf(angle), sinf(angle));
				f32 time_offset = rand_f32() * 0.1f;
				instance.start_velocity = velocity;
				instance.start_pos = start + offset;
				instance.start_time = start_time + time_offset;
				instance.end_time = end_time + time_offset;
				particles_add(particles, instance);
			}
			break;
		}
		case ANIM_EXCHANGE_PARTICLES: {
			Particle_Instance instance = {};
			Particles *particles = &draw->renderer.particles;
			instance.end_color = v4(1.0f, 1.0f, 1.0f, 1.0f);

			f32 start_time = anim->start_time;
			f32 duration = constants.anims.exchange.particle_duration;

			u32 num_particles = constants.anims.exchange.num_particles;
			for (u32 i = 0; i < num_particles; ++i) {
				instance.start_time = start_time + rand_f32() * 0.1f;
				instance.end_time = start_time + duration + rand_f32() * 0.1f;
				f32 duration = instance.end_time - instance.start_time;

				f32 col = uniform_f32(0.8f, 1.0f);
				instance.start_color = v4(col, col, col, 1.0f);

				v2 offset = v2(uniform_f32(-0.5f, 0.5f), uniform_f32(0.0f, 0.5f));
				instance.start_pos = anim->exchange_particles.pos + offset;
				instance.start_velocity = v2(0.0f, -1.0f / duration);
				particles_add(particles, instance);
			}
			break;
		}
		case ANIM_POLYMORPH_PARTICLES: {
			u32 num_particles = constants.anims.polymorph.num_particles;

			Particles *particles = &draw->renderer.particles;
			Particle_Instance instance = {};
			instance.start_pos = anim->polymorph_particles.pos;
			// instance.start_time = time;
			// instance.end_time = time + constants.anims.polymorph.duration;
			instance.start_time = anim->start_time;
			instance.end_time = anim->start_time + constants.anims.polymorph.duration;
			f32 sin_inner_coeff = constants.anims.polymorph.rotation_speed;
			sin_inner_coeff /= (PI_F32 / 2.0f);
			instance.sin_inner_coeff = v2(sin_inner_coeff, sin_inner_coeff);

			for (u32 i = 0; i < num_particles; ++i) {
				instance.start_color = {
					uniform_f32(0.8f, 1.0f),
					uniform_f32(0.8f, 1.0f),
					uniform_f32(0.8f, 1.0f),
					1.0f
				};
				instance.end_color = {
					uniform_f32(0.8f, 1.0f),
					uniform_f32(0.8f, 1.0f),
					uniform_f32(0.8f, 1.0f),
					1.0f
				};
				f32 angle = 2.0f * PI_F32 * rand_f32();
				instance.sin_phase_offset = v2(PI_F32 / 2.0f, 0.0f) + angle;
				f32 radius = uniform_f32(constants.anims.polymorph.min_radius,
								constants.anims.polymorph.max_radius);
				instance.sin_outer_coeff = v2(radius, radius);
				particles_add(particles, instance);
			}
			break;
		}
		case ANIM_SHOOT_WEB_PARTICLES: {
			u32 num_particles = constants.anims.shoot_web.num_particles;

			v2 start = anim->shoot_web_particles.start;
			v2 end = anim->shoot_web_particles.end;
			v2 velocity = (end - start) / constants.anims.shoot_web.shot_duration;

			Particles *particles = &draw->renderer.particles;
			Particle_Instance instance = {};
			// instance.start_time = time;
			// instance.end_time = time + constants.anims.shoot_web.shot_duration;
			instance.start_time = anim->start_time;
			instance.end_time = anim->start_time + constants.anims.shoot_web.shot_duration;

			for (u32 i = 0; i < num_particles; ++i) {
				f32 color = uniform_f32(0.8f, 1.0f);
				instance.start_color = v4(color, color, color, 1.0f);
				color = uniform_f32(0.8f, 1.0f);
				instance.end_color = v4(color, color, color, 1.0f);

				f32 h = 1.0f;
				f32 d = constants.anims.shoot_web.shot_duration;
				f32 v = - (4.0f*h) / d;
				f32 a = (8.0f*h) / (d*d);

				instance.acceleration = v2(0.0f, a);
				instance.start_velocity = velocity + v2(0.0f, v);

				f32 angle = 2.0f * PI_F32 * rand_f32();
				f32 mag = 0.5f * rand_f32();

				instance.start_pos = start + v2(cosf(angle), sinf(angle)) * mag;
				particles_add(particles, instance);
			}
			break;
		}
		case ANIM_TURN_VISIBLE:
		case ANIM_ADD_ITEM:
		case ANIM_ADD_CREATURE:
		case ANIM_PROJECTILE_EFFECT_24:
		case ANIM_PROJECTILE_EFFECT_32:
		case ANIM_TEXT:
		case ANIM_CAMERA_SHAKE:
		case ANIM_DEATH:
		case ANIM_EXCHANGE:
		case ANIM_BLINK:
		case ANIM_POLYMORPH:
		case ANIM_FIELD_OF_VISION_CHANGED:
		case ANIM_OPEN_DOOR:
			break;
		default:
			ASSERT(0);
			break;
		}
	}

	// clear up finished animations
	for (u32 i = 0; i < world_dynamic_anims.len; ) {
		auto anim = &world_dynamic_anims[i];
		if (anim->start_time + anim->duration > dyn_time) {
			++i;
			continue;
		}

		switch (anim->type) {
		case ANIM_TURN_VISIBLE: {
			auto static_anim = get_static_anim(anim_state, anim->turn_visible.entity_id);
			if (static_anim) {
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					if (static_anim->modifiers[i].type == ANIM_MOD_COLOR) {
						static_anim->modifiers.remove(i);
						break;
					}
				}
			}
			break;
		}
		case ANIM_MOVE: {
			auto static_anim = get_static_anim(anim_state, anim->move.entity_id);
			if (static_anim) {
				static_anim->idle.offset = time;
				static_anim->idle.duration = uniform_f32(0.8f, 1.2f);
				static_anim->world_coords = anim->move.end;
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					if (static_anim->modifiers[i].type == ANIM_MOD_POS) {
						static_anim->modifiers.remove(i);
						break;
					}
				}
			}
			break;
		}
		case ANIM_MOVE_BLOCKED: {
			auto static_anim = get_static_anim(anim_state, anim->move.entity_id);
			if (static_anim) {
				static_anim->idle.offset = time;
				static_anim->idle.duration = uniform_f32(0.8f, 1.2f);
				static_anim->world_coords = anim->move.start;
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					if (static_anim->modifiers[i].type == ANIM_MOD_POS) {
						static_anim->modifiers.remove(i);
						break;
					}
				}
			}
			break;
		}
		case ANIM_DEATH: {
			Entity_ID entity_id = anim->death.entity_id;
			for (u32 i = 0; i < world_static_anims.len; ) {
				if (world_static_anims[i].entity_id == entity_id) {
					world_static_anims.remove(i);
					continue;
				}
				++i;
			}
			break;
		}
		case ANIM_DROP_TILE: {
			Entity_ID entity_id = anim->drop_tile.entity_id;
			for (u32 i = 0; i < world_static_anims.len; ) {
				if (world_static_anims[i].entity_id == entity_id) {
					world_static_anims.remove(i);
					continue;
				}
				++i;
			}
			break;
		}
		case ANIM_EXCHANGE: {
			Entity_ID a_id = anim->exchange.a;
			Entity_ID b_id = anim->exchange.b;
			auto a = get_static_anim(anim_state, a_id);
			auto b = get_static_anim(anim_state, b_id);
			ASSERT(a && b);
			// XXX - not sure what to do about this
			ASSERT(a->type == ANIM_CREATURE_IDLE);
			ASSERT(b->type == ANIM_CREATURE_IDLE);
			v2 tmp = a->world_coords;
			a->world_coords = b->world_coords;
			b->world_coords = tmp;
			break;
		}
		case ANIM_BLINK: {
			Entity_ID e_id = anim->blink.entity_id;
			auto caster_anim = get_static_anim(anim_state, e_id);
			ASSERT(caster_anim);
			caster_anim->world_coords = anim->blink.target;
			break;
		}
		case ANIM_SLIME_SPLIT: {
			auto static_anim = get_static_anim(anim_state, anim->slime_split.new_id);
			ASSERT(static_anim);
			for (u32 i = 0; i < static_anim->modifiers.len; ) {
				auto modifier = &static_anim->modifiers[i];
				switch (modifier->type) {
				case ANIM_MOD_POS:
					static_anim->modifiers.remove(i);
					continue;
				}
				++i;
			}
			break;
		}
		case ANIM_POLYMORPH: {
			Entity_ID entity_id = anim->polymorph.entity_id;
			auto static_anim = get_static_anim(anim_state, entity_id);
			static_anim->sprite_coords = anim->polymorph.new_sprite_coords;
			break;
		}
		case ANIM_CREATURE_DROP_IN: {
			auto static_anim = get_static_anim(anim_state, anim->creature_drop_in.entity_id);
			ASSERT(static_anim);
			for (u32 i = 0; i < static_anim->modifiers.len; ) {
				switch (static_anim->modifiers[i].type) {
				case ANIM_MOD_COLOR:
					static_anim->modifiers.remove(i);
					continue;
				case ANIM_MOD_POS:
					static_anim->modifiers.remove(i);
					continue;
				}
				++i;
			}
			break;
		}
		case ANIM_TURN_INVISIBLE: {
			auto static_anim = get_static_anim(anim_state, anim->turn_invisible.entity_id);
			if (static_anim) {
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					auto modifier = &static_anim->modifiers[i];
					if (modifier->type == ANIM_MOD_COLOR) {
						modifier->color.color = v4(1.0f, 1.0f, 1.0f, constants.anims.turn_invisible.visibility);
					}
				}
			}
			break;
		}
		case ANIM_ADD_ITEM: {
			auto static_anim = world_static_anims.append();
			static_anim->type = ANIM_TILE_STATIC;
			static_anim->sprite_coords = anim->add_item.sprite_coords;
			static_anim->world_coords = anim->add_item.pos;
			static_anim->entity_id = anim->add_item.entity_id;
			// XXX -- ugh
			static_anim->depth_offset = constants.z_offsets.door;
			break;
		}
		case ANIM_ADD_CREATURE: {
			auto static_anim = world_static_anims.append();
			static_anim->type = ANIM_CREATURE_IDLE;
			static_anim->sprite_coords = anim->add_creature.sprite_coords;
			static_anim->world_coords = anim->add_creature.pos;
			static_anim->entity_id = anim->add_creature.entity_id;
			static_anim->depth_offset = constants.z_offsets.character;
			break;
		}
		// TODO -- clean up particles
		case ANIM_EXCHANGE_PARTICLES:
		case ANIM_BLINK_PARTICLES:
		case ANIM_POLYMORPH_PARTICLES:
		case ANIM_HEAL_PARTICLES:
		case ANIM_SHOOT_WEB_PARTICLES:
			break;
		case ANIM_FIELD_OF_VISION_CHANGED:
		case ANIM_TEXT:
		case ANIM_PROJECTILE_EFFECT_24:
		case ANIM_PROJECTILE_EFFECT_32:
		case ANIM_CAMERA_SHAKE:
			break;
		default:
			// Should explicitly decide whether a dynamic anim does anything on cleanup or not
			ASSERT(0);
			break;
		}
		world_dynamic_anims.remove(i);
	}

	f32 camera_offset_mag = 0.0f;
	// Draw dynamic animations
	for (u32 i = 0; i < world_dynamic_anims.len; ++i) {
		auto anim = &world_dynamic_anims[i];

		f32 t = dyn_time - anim->start_time;
		if (t < 0.0f) {
			continue;
		}
		f32 dt = t / anim->duration;
		ASSERT(t >= 0.0f);
		ASSERT(0.0f <= dt && dt <= 1.0f);

		switch (anim->type) {
		case ANIM_MOVE: {
			auto static_anim = get_static_anim(anim_state, anim->move.entity_id);
			if (static_anim) {
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					auto modifier = &static_anim->modifiers[i];
					if (modifier->type == ANIM_MOD_POS) {
						v2 start = anim->move.start;
						v2 end = anim->move.end;
						modifier->pos.pos = (end - start) * dt;
						modifier->pos.y_offset = - constants.anims.move.jump_height * 4.0f * dt*(1.0f - dt);
					}
				}
			}
			break;
		}
		case ANIM_MOVE_BLOCKED: {
			auto static_anim = get_static_anim(anim_state, anim->move.entity_id);
			if (static_anim) {
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					auto modifier = &static_anim->modifiers[i];
					if (modifier->type == ANIM_MOD_POS) {
						v2 start = anim->move.start;
						v2 end = anim->move.end;
						f32 w = dt > 0.5f ? 1.0f - dt : dt;
						modifier->pos.pos = (end - start) * w;
						modifier->pos.y_offset = - constants.anims.move.jump_height * 4.0f * dt*(1.0f - dt);
					}
				}
			}
			break;
		}
		case ANIM_CREATURE_DROP_IN: {
			auto static_anim = get_static_anim(anim_state, anim->creature_drop_in.entity_id);
			ASSERT(static_anim);
			for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
				auto modifier = &static_anim->modifiers[i];
				switch (modifier->type) {
				case ANIM_MOD_COLOR:
					modifier->color.color = v4(1.0f, 1.0f, 1.0f, dt);
					break;
				case ANIM_MOD_POS:
					modifier->pos.pos = v2(0.0f, 0.0f);
					modifier->pos.y_offset = - (1.0f - dt) * constants.anims.creature_drop_in.drop_height * 24.0f;
					break;
				}
			}
			break;
		}
		case ANIM_DROP_TILE: {
			auto static_anim = get_static_anim(anim_state, anim->drop_tile.entity_id);
			for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
				auto modifier = &static_anim->modifiers[i];
				switch (modifier->type) {
				case ANIM_MOD_POS:
					modifier->pos.pos = v2(0.0f, 0.0f);
					modifier->pos.y_offset = dt*dt * constants.anims.drop_tile.drop_distance * 24.0f;
					break;
				case ANIM_MOD_COLOR: {
					f32 c = 1.0f - dt;
					modifier->color.color = v4(c, c, c, 1.0f);
					break;
				}
				}
			}
			break;
		}
		case ANIM_TURN_INVISIBLE: {
			auto static_anim = get_static_anim(anim_state, anim->turn_visible.entity_id);
			if (static_anim) {
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					auto modifier = &static_anim->modifiers[i];
					if (modifier->type == ANIM_MOD_COLOR) {
						modifier->color.color = v4(1.0f, 1.0f, 1.0f, lerp(1.0f, constants.anims.turn_invisible.visibility, dt));
					}
				}
			}
			break;
		}
		case ANIM_TURN_VISIBLE: {
			auto static_anim = get_static_anim(anim_state, anim->turn_visible.entity_id);
			if (static_anim) {
				for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
					auto modifier = &static_anim->modifiers[i];
					if (modifier->type == ANIM_MOD_COLOR) {
						modifier->color.color = v4(1.0f, 1.0f, 1.0f, lerp(constants.anims.turn_invisible.visibility, 1.0f, dt));
					}
				}
			}
			break;
		}
		case ANIM_PROJECTILE_EFFECT_24: {
			Sprite_Sheet_Instance instance = {};
			instance.sprite_pos = anim->projectile.sprite_coords;
			instance.world_pos = lerp(anim->projectile.start, anim->projectile.end, dt);
			instance.sprite_id = 0;
			instance.depth_offset = 2.0f;
			instance.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
			instance.sprite_pos = anim->projectile.sprite_coords;

			sprite_sheet_instances_add(&draw->effects_24, instance);
			break;
		}
		case ANIM_PROJECTILE_EFFECT_32: {
			Sprite_Sheet_Instance instance = {};
			instance.sprite_pos = anim->projectile.sprite_coords;
			instance.world_pos = lerp(anim->projectile.start, anim->projectile.end, dt);
			instance.sprite_id = 0;
			instance.depth_offset = 2.0f;
			instance.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
			instance.sprite_pos = anim->projectile.sprite_coords;

			sprite_sheet_instances_add(&draw->effects_32, instance);
			break;
		}
		case ANIM_TEXT: {
			u32 width = 1, height = 0;
			for (u8 *p = anim->text.caption; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				width += glyph.dimensions.w - 1;
				height = max(height, glyph.dimensions.h);
			}

			Sprite_Sheet_Font_Instance instance = {};
			instance.world_pos = anim->text.pos + 0.5f;
			instance.world_pos.y -= dt;
			instance.world_offset = v2(-(f32)(width / 2), -(f32)(height / 2));
			instance.zoom = 1.0f;
			instance.color_mod = anim->text.color;
			for (u8 *p = anim->text.caption; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				instance.glyph_pos = (v2)glyph.top_left;
				instance.glyph_size = (v2)glyph.dimensions;

				sprite_sheet_font_instances_add(&draw->boxy_bold, instance);

				instance.world_offset.x += (f32)glyph.dimensions.w - 1.0f;
			}

			break;
		}
		case ANIM_CAMERA_SHAKE: {
			f32 power = anim->camera_shake.power * (dt - 1.0f) * (dt - 1.0f);
			camera_offset_mag = max(camera_offset_mag, power);
			break;
		}
		case ANIM_SLIME_SPLIT: {
			auto static_anim = get_static_anim(anim_state, anim->slime_split.new_id);
			for (u32 i = 0; i < static_anim->modifiers.len; ++i) {
				auto modifier = &static_anim->modifiers[i];
				if (modifier->type == ANIM_MOD_POS) {
					v2 start = anim->slime_split.start;
					v2 end = anim->slime_split.end;
					modifier->pos.pos = (start - end) * (1.0f - dt);
					modifier->pos.y_offset = 0.0f;
				}
			}
			break;
		}
		case ANIM_FIELD_OF_VISION_CHANGED:
			break;

		}
	}

	// draw tile animations
	for (u32 i = 0; i < world_static_anims.len; ++i) {
		auto anim = &world_static_anims[i];
		switch (anim->type) {
		case ANIM_TILE_STATIC: {
			Sprite_Sheet_Instance tile = {};
			tile.sprite_pos = anim->sprite_coords;
			tile.world_pos = anim->world_coords;
			tile.sprite_id = anim->entity_id;
			tile.depth_offset = anim->depth_offset;
			tile.y_offset = 0.0f;
			tile.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);

			for (u32 i = 0; i < anim->modifiers.len; ++i) {
				auto modifier = &anim->modifiers[i];
				switch (modifier->type) {
				case ANIM_MOD_COLOR:
					tile.color_mod = modifier->color.color;
					break;
				case ANIM_MOD_POS:
					tile.world_pos += modifier->pos.pos;
					tile.y_offset += modifier->pos.y_offset;
					break;
				}
			}

			sprite_sheet_instances_add(&draw->tiles, tile);
			break;
		}
		case ANIM_TILE_LIQUID: {
			Sprite_Sheet_Instance ti = {};

			f32 dt = time + anim->tile_liquid.offset;
			dt = fmodf(dt, anim->tile_liquid.duration) / anim->tile_liquid.duration;
			if (dt > 0.5f) {
				ti.sprite_pos = anim->sprite_coords;
			} else {
				ti.sprite_pos = anim->tile_liquid.second_sprite_coords;
			}
			ti.world_pos = anim->world_coords;
			ti.sprite_id = anim->entity_id;
			ti.depth_offset = anim->depth_offset;
			ti.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
			sprite_sheet_instances_add(&draw->tiles, ti);

			break;
		}
		case ANIM_CREATURE_IDLE: {
			v2 sprite_pos = anim->sprite_coords;
			f32 dt = time + anim->idle.offset;
			dt = fmodf(dt, anim->idle.duration) / anim->idle.duration;
			if (dt > 0.5f) {
				sprite_pos.y += 1.0f;
			}

			v2 world_pos = anim->world_coords;

			Sprite_Sheet_Instance shadow = {};
			shadow.sprite_pos = v2(4.0f, 22.0f);
			shadow.world_pos = world_pos;
			shadow.sprite_id = anim->entity_id;
			shadow.depth_offset = constants.z_offsets.character_shadow;
			shadow.y_offset = -3.0f;
			shadow.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);

			Sprite_Sheet_Instance creature = {};
			creature.sprite_pos = sprite_pos;
			creature.world_pos = world_pos;
			creature.sprite_id = anim->entity_id;
			creature.depth_offset = constants.z_offsets.character;
			creature.y_offset = -6.0f;
			creature.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);

			for (u32 i = 0; i < anim->modifiers.len; ++i) {
				auto modifier = &anim->modifiers[i];
				switch (modifier->type) {
				case ANIM_MOD_COLOR:
					creature.color_mod = modifier->color.color;
					break;
				case ANIM_MOD_POS:
					shadow.world_pos += modifier->pos.pos;
					creature.world_pos += modifier->pos.pos;
					creature.y_offset += modifier->pos.y_offset;
					break;
				}
			}

			sprite_sheet_instances_add(&draw->creatures, shadow);
			sprite_sheet_instances_add(&draw->creatures, creature);
			break;
		}
		case ANIM_WATER_EDGE: {
			Sprite_Sheet_Instance water_edge = {};
			water_edge.sprite_pos = anim->sprite_coords;
			water_edge.world_pos = anim->world_coords;
			water_edge.sprite_id = anim->entity_id;
			water_edge.depth_offset = anim->depth_offset;
			v4_u8 c = anim->water_edge.color;
			water_edge.color_mod = {
				(f32)c.r / 256.0f,
				(f32)c.g / 256.0f,
				(f32)c.b / 256.0f,
				(f32)c.a / 256.0f,
			};
			sprite_sheet_instances_add(&draw->water_edges, water_edge);
			break;
		}
		}
	}

	if (camera_offset_mag > 0.0f) {
		f32 theta = uniform_f32(0.0f, 2.0f * PI_F32);
		m2 m = m2::rotation(theta);
		v2 v = m * v2(camera_offset_mag, 0.0f);
		anim_state->camera_offset = v;
	}

	// Do render job with new abstracted renderer
	{
		begin(r, RENDER_EVENT_WORLD);

		clear_uint(r, TARGET_TEXTURE_SPRITE_ID);
		clear_rtv(r, TARGET_TEXTURE_WORLD_STATIC,  v4(0.0f, 0.0f, 0.0f, 0.0f));
		clear_rtv(r, TARGET_TEXTURE_WORLD_DYNAMIC, v4(0.0f, 0.0f, 0.0f, 0.0f));
		clear_depth(r, TARGET_TEXTURE_SPRITE_DEPTH, 0.0f);

		Render_Push_World_Sprites_Desc desc = {};
		desc.output_sprite_id_tex_id = TARGET_TEXTURE_SPRITE_ID;
		desc.depth_tex_id = TARGET_TEXTURE_SPRITE_DEPTH;
		desc.constants.screen_size = v2(256.0f*24.0f, 256.0f*24.0f);
		desc.constants.sprite_size = v2(24.0f, 24.0f);
		desc.constants.world_tile_size = v2(24.0f, 24.0f);
		desc.constants.tex_size = v2(1.0f, 1.0f);

		begin(r, RENDER_EVENT_WORLD_STATIC);

		desc.output_tex_id = TARGET_TEXTURE_WORLD_STATIC;
		desc.sprite_tex_id = SOURCE_TEXTURE_TILES;
		desc.instances = draw->tiles.instances;
		push_world_sprites(r, &desc);

		desc.sprite_tex_id = SOURCE_TEXTURE_EDGES;
		desc.instances = draw->water_edges.instances;
		push_world_sprites(r, &desc);

		end(r, RENDER_EVENT_WORLD_STATIC);
		begin(r, RENDER_EVENT_WORLD_DYNAMIC);

		desc.output_tex_id = TARGET_TEXTURE_WORLD_DYNAMIC;
		desc.sprite_tex_id = SOURCE_TEXTURE_CREATURES;
		desc.instances = draw->creatures.instances;
		push_world_sprites(r, &desc);

		desc.sprite_tex_id = SOURCE_TEXTURE_EFFECTS_24;
		desc.instances = draw->effects_24.instances;
		push_world_sprites(r, &desc);

		desc.constants.sprite_size = v2(32.0f, 32.0f);
		desc.sprite_tex_id = SOURCE_TEXTURE_EFFECTS_32;
		desc.instances = draw->effects_32.instances;
		push_world_sprites(r, &desc);
		desc.constants.sprite_size = v2(24.0f, 24.0f);

		Render_Push_World_Particles_Desc particles_desc = {};
		particles_desc.output_tex_id = TARGET_TEXTURE_WORLD_DYNAMIC;
		particles_desc.world_size = v2(256.0f, 256.0f);
		particles_desc.tile_size = v2(24.0f, 24.0f);
		particles_desc.time = dyn_time;
		particles_desc.instances = draw->renderer.particles.particles;
		push_world_particles(r, &particles_desc);

		Render_Push_World_Font_Desc font_desc = {};
		font_desc.output_tex_id = TARGET_TEXTURE_WORLD_DYNAMIC;
		font_desc.font_tex_id = SOURCE_TEXTURE_BOXY_BOLD;
		font_desc.constants.screen_size = v2(256.0f*24.0f, 256.0f*24.0f);
		font_desc.constants.sprite_size = v2(24.0f, 24.0f);
		font_desc.constants.world_tile_size = v2(24.0f, 24.0f);
		font_desc.constants.tex_size = v2(1.0f, 1.0f);
		font_desc.instances = draw->boxy_bold.instances;
		push_world_font(r, &font_desc);

		end(r, RENDER_EVENT_WORLD_DYNAMIC);
		end(r, RENDER_EVENT_WORLD);
	}

	// play sounds
	auto &sounds = anim_state->sound_anims;
	for (u32 i = 0; i < sounds.len; ) {
		auto sound = &anim_state->sound_anims[i];
		if (sound->start_time <= dyn_time) {
			sound_player->play(sound->sound_id);
			sounds.remove(i);
			continue;
		}
		++i;
	}
}

bool is_animating(Anim_State* anim_state)
{
	if (anim_state->world_dynamic_anims) {
		return true;
	}
	return false;
}
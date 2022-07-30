#!/usr/bin/python3

import argparse

parser = argparse.ArgumentParser(description='Write appearance metadata.')
parser.add_argument(
	'-o', '--output',
	metavar='output_filename',
	type=str,
	nargs=1,
	required=True,
	help='Filename for the output header.',
)

args = parser.parse_args()
out_filename = args.output[0]

from collections import defaultdict

creatures = [
	'Male Knight',
	'Male Thief',
	'Male Ranger',
	'Male Wizard',
	'Male Priest',
	'Male Shaman',
	'Male Berserker',
	'Male Swordsman',
	'Male Paladin',
	'Female Knight',
	'Female Thief',
	'Female Ranger',
	'Female Wizard',
	'Female Priest',
	'Female Shaman',
	'Female Berserker',
	'Female Swordsman',
	'Female Paladin',

	'Western Bandit',
	'Western Hooded Human',
	'Western Male Human',
	'Western Female Human',
	'Western Merchant',
	'Butcher',
	'Chef',
	'Bishop',
	'Western King',
	'Western Queen',
	'Western Prince',
	'Western Princess',
	'Red Male Guard',
	'Red Female Guard',
	'Red Knight',
	'Blue Male Guard',
	'Blue Female Guard',
	'Blue Knight',

	'Eastern Bandit',
	'Eastern Hooded Human',
	'Eastern Male Human',
	'Female Human',
	'Eastern Merchant',
	'Slave',
	'Alchemist',
	'Prophet',
	'Eastern King',
	'Eastern Queen',
	'Eastern Prince',
	'Eastern Princess',
	'Male Guard',
	'Female Guard',
	'Knight',
	'Male Guard Alt',
	'Female Guard Alt',
	'Knight Alt',

	'Assassin',
	'Bandit',
	'Dwarf',
	'Dwarf Alt',
	'Dwarf Priest',
	'Drow Assassin',
	'Drow Fighter',
	'Drow Ranger',
	'Drow Mage',
	'Drow Sorceress',
	'Male High Elf Fighter',
	'Male High Elf Shield Fighter',
	'Male High Elf Ranger',
	'Male High Elf Mage',
	'Female High Elf Fighter',
	'Female High Elf Shield Fighter',
	'Female High Elf Ranger',
	'Female High Elf Mage',

	'Male Wood Elf Fighter',
	'Male Wood Elf Shield Fighter',
	'Male Wood Elf Ranger',
	'Male Wood Elf Druid',
	'Female Wood Elf Fighter',
	'Female Wood Elf Shield Fighter',
	'Female Wood Elf Ranger',
	'Female Wood Elf Druid',
	'Lizardman Warrior',
	'Lizardman Archer',
	'Lizardman Captain',
	'Lizardman Shaman',
	'Lizardman High Shaman',
	'Gnome Fighter 1',
	'Gnome Fighter 2',
	'Gnome Fighter 3',
	'Gnome Wizard',
	'Gnome Wizard Alt',

	'Gnoll Fighter',
	'Gnoll Fighter Alt',
	'Gnoll Fighter Captain',
	'Gnoll Shaman',
	'Minotaur Axe',
	'Minotaur Club',
	'Minotaur Alt',
	'Elder Demon',
	'Fire Demon',
	'Horned Demon',
	'Stone Golem',
	'Mud Golem',
	'Flesh Golem',
	'Lava Golem',
	'Bone Golem',
	'Djinn',
	'Treant',
	'Mimic',

	'Purple Slime',
	'Green Slime',
	'Black Bat',
	'Red Bat',
	'Beholder',
	'Red Spider',
	'Black Spider',
	'Grey Rat',
	'Brown Rat',
	'Cobra',
	'Beetle',
	'Fire Beetle',
	'Grey Wolf',
	'Brown Wolf',
	'Black Wolf',
	'Dove',
	'Blue Bird',
	'Raven',

	'Goblin Fighter',
	'Goblin Archer',
	'Goblin Captain',
	'Goblin King',
	'Goblin Mystic',
	'Orc Fighter',
	'Orc Captain',
	'Orc Mystic',
	'Troll',
	'Troll Captain',
	'Cyclops',
	'Cyclops Alt',
	'Red Death Knight',
	'Blue Death Knight',
	'Gold Death Knight',
	'Earth Elemental',
	'Water Elemental',
	'Air Elemental',

	'Zombie',
	'Headless Zombie',
	'Skeleton',
	'Skeleton Archer',
	'Skeleton Warrior',
	'Shadow',
	'Ghost',
	'Mummy',
	'Pharoah',
	'Necromancer',
	'Dark Wizard',
	'Death',
	'Vampire',
	'Vampire Alt',
	'Vampire Lord',
	'Witch',
	'Frost Witch',
	'Green Witch',

	'Red Dragon',
	'Purple Dragon',
	'Gold Dragon',
	'Green Dragon',
	'Yeti',
	'Yeti Alt',
	'Giant Leech',
	'Giant Worm',
	'Brown Bear',
	'Grey Bear',
	'Polar Bear',
	'Red Scorpion',
	'Brown Scorpion',
	'Black Scorpion',
	'Ettin',
	'Ettin Alt',
	'Pixie',
	'Imp',

	'Blue Wisp',
	'Red Wisp',
	'Turnip',
	'Rotten Turnip',
	'Fire Minion',
	'Ice Minion',
	'Smoke Minion',
	'Mud Minion',
	'Eye',
	'Eyes',
	'Red Specter',
	'Blue Specter',
	'Brown Specter',
	'Blue Jelly',
	'Green Jelly',
	'Red Jelly',
	'Red Flame',
	'Blue Flame',
]

N, E, S, W = 1, 2, 4, 8
wall_connect_order = [
	0,
	E,
	E | W,
	W,
	S,
	S | N,
	N,
	S | E,
	S | W,
	N | E,
	N | W,
	N | E | S | W,
	S | E | W,
	N | S | W,
	N | S | E,
	N | E | W,
]

connect_order_lookup = [wall_connect_order.index(i) for i in range(len(wall_connect_order))]

walls = [
	'Rock 1',
	'Rock 2',
	'Brown Rock',
	'Points',
	'Wood',
	'Fancy',
	'Teal',
	'Navy',
	'Gold',
	'Rock 3',
	'Brown Rock 2',
	'Mossy Rock',
	'Cave',
	'Hedge',
	'Snow Rock',
	'Fence',
	'Snow Rock 2',
	'Snow Rock 3',
	'Desert',
	'Ruined Low Yellow',
	'Ruined Low Grey',
	'Ruined Low Red',
]

# (name [(pos, weight)])
floors = [
	('Rock', [(3, 0, 0.8), (4, 0, 0.1), (5, 0, 0.05), (6, 1, 0.05)]),
]

doors = (
	('Wooden Plain',        (28, 2)),
	('Wooden Open',         (29, 2)),
	('Wooden Barred',       (30, 2)),
	('Wooden No Handle',    (31, 2)),
	('Wooden Metal Barred', (32, 2)),
	('Wooden Broken',       (33, 2)),
	('Wooden Ajar',         (34, 2)),
	('Stone',               (35, 2)),
	('Stone Open',          (36, 2)),
	('Ice',                 (37, 2)),
	('Ice Open',            (38, 2)),
	('Wooden Boarded',      (39, 2)),
	('Wooden Boarded Open', (40, 2)),
	('Wooden Portal',       (41, 2)),
	('Cage',                (28, 3)),
	('Cage Open',           (29, 3)),
	('Glass',               (30, 3)),
)

liquids = (
	('Water',  ( 4, 30)),
	('Slime',  ( 7, 30)),
	('Lava',   (10, 30)),
	('Sewage', (13, 30)),
)

items = (
	('Gravestone',         (28, 0)),
	('Gravestone Cracked', (29, 0)),
	('Gravestone Broken',  (30, 0)),
	('Bones 1',            (31, 0)),
	('Bones 2',            (32, 0)),
	('Bones 3',            (33, 0)),
	('Bones 4',            (34, 0)),
	('Bones 5',            (35, 0)),
	('Bones 6',            (36, 0)),
	('Skeleton',           (37, 0)),
	('Fire 1',             (38, 0)),
	('Fire 2',             (39, 0)),
	('Brazier 1',          (40, 0)),
	('Brazier 2',          (41, 0)),

	('Spiderweb Top Left',     (28, 1)),
	('Spiderweb Top Right',    (29, 1)),
	('Spiderweb Bottom Right', (30, 1)),
	('Spiderweb Bottom Left',  (31, 1)),
	('Spiderweb 1',            (32, 1)),
	('Spiderweb 2',            (33, 1)),
	('Brazier 3',              (40, 1)),
	('Brazier 4',              (41, 1)),

	('Chest',            (31, 3)),
	('Chest Gold',       (32, 3)),
	('Chest Trap',       (33, 3)),
	('Chest Empty',      (34, 3)),
	('Coffin Closed',    (35, 3)),
	('Coffin Skeleton',  (36, 3)),
	('Coffin Open',      (37, 3)),
	('Barrel',           (38, 3)),
	('Barrel Broken',    (39, 3)),
	('Bucket 1',         (40, 3)),
	('Bucket 2',         (41, 3)),

	('Trapdoor Closed',  (28, 4)),
	('Trapdoor Open',    (29, 4)),
	('Bookshelf',        (30, 4)),
	('Bookshelf Empty',  (31, 4)),
	('Table',            (32, 4)),
	('Table Notes',      (33, 4)),
	('Throne 1',         (34, 4)),
	('Throne 2',         (35, 4)),
	('Weapon Rack 1',    (36, 4)),
	('Weapon Rack 2',    (37, 4)),
	('Chest 2 Closed',   (38, 4)),
	('Chest 2 Open',     (39, 4)),
	('Crate Open',       (40, 4)),
	('Crate Closed',     (41, 4)),

	('Cauldron 1',        (30, 5)),
	('Cauldron 2',        (31, 5)),
	('Statue Wizard',     (32, 5)),
	('Statue Broken',     (33, 5)),
	('Statue Ranger',     (34, 5)),
	('Stateu Paladin',    (35, 5)),

	('Trap Spikes',       (29, 6)),
	('Trap Circles',      (29, 6)),
	('Trap Hex',          (30, 6)),
	('Trap Crossbones',   (31, 6)),
	('Trap Beartrap',     (32, 6)),
	('Trap Hole',         (33, 6)),
	('Grate 1',           (34, 6)),
	('Grate 2',           (35, 6)),
	('Vase 1',            (36, 6)),
	('Vase 1 Cracked',    (37, 6)),
	('Vase 1 Broken',     (38, 6)),
	('Vase 2',            (39, 6)),
	('Vase 2 Cracked',    (40, 6)),
	('Vase 2 Broken',     (41, 6)),

	('Fountain',          (29, 7)),
	('Fountain Off',      (30, 7)),
	('Alter 1',           (31, 7)),
	('Alter 2',           (32, 7)),
	('Alter 3',           (33, 7)),
	('Alter 4',           (34, 7)),
	('Alter 5',           (35, 7)),
	('Alter 6',           (36, 7)),
	('Vase 3',            (37, 7)),
	('Vase 3 Cracked',    (38, 7)),
	('Vase 3 Broken',     (39, 7)),
	('Vase 4',            (40, 7)),
	('Vase 4 Cracked',    (41, 7)),
	('Vase 4 Broken',     (42, 7)),
)

output_template = '''
#ifndef APPEARANCE_H
#define APPEARANCE_H

#include "prelude.h"

#define APPEARANCE_N 1
#define APPEARANCE_E 2
#define APPEARANCE_S 4
#define APPEARANCE_W 8

enum Appearance
{{
	APPEARANCE_NONE,

	{creature_appearance_names},

	{wall_appearance_names},

	{floor_appearance_names},

	{door_appearance_names},

	{liquid_appearance_names},

	{item_appearance_names},

	NUM_APPEARANCES,
}};

u8 appearance_is_creature(Appearance appearance);
u8 appearance_is_wall(Appearance appearance);
u8 appearance_is_floor(Appearance appearance);
u8 appearance_is_door(Appearance appearance);
u8 appearance_is_liquid(Appearance appearance);
u8 appearance_is_item(Appearance appearance);

v2 appearance_get_creature_sprite_coords(Appearance appearance);
v2 appearance_get_wall_sprite_coords(Appearance appearance, u8 connection_mask);
u8 appearance_get_wall_id(Appearance appearance);
Appearance appearance_wall_id_to_appearance(u8 wall_id);
v2 appearance_get_floor_sprite_coords(Appearance appearance);
u8 appearance_get_floor_id(Appearance appearance);
Appearance appearance_floor_id_to_appearance(u8 floor_id);
v2 appearance_get_door_sprite_coords(Appearance appearance);
v2 appearance_get_liquid_sprite_coords(Appearance appearance);
u8 appearance_get_liquid_id(Appearance appearance);
v2 appearance_get_item_sprite_coords(Appearance appearance);

#ifndef JFG_HEADER_ONLY
u8 appearance_is_creature(Appearance appearance)
{{
	return appearance >= {min_creature_appearance} && appearance <= {max_creature_appearance};
}}

u8 appearance_is_wall(Appearance appearance)
{{
	return appearance >= {min_wall_appearance} && appearance <= {max_wall_appearance};
}}

u8 appearance_is_floor(Appearance appearance)
{{
	return appearance >= {min_floor_appearance} && appearance <= {max_floor_appearance};
}}

u8 appearance_is_door(Appearance appearance)
{{
	return appearance >= {min_door_appearance} && appearance <= {max_door_appearance};
}}

u8 appearance_is_liquid(Appearance appearance)
{{
	return appearance >= {min_liquid_appearance} && appearance <= {max_liquid_appearance};
}}

u8 appearance_is_item(Appearance appearance)
{{
	return appearance >= {min_item_appearance} && appearance <= {max_item_appearance};
}}

v2 appearance_get_creature_sprite_coords(Appearance appearance)
{{
	u32 index = appearance - {min_creature_appearance};
	return {{ (f32)(index % 18), 2.0f * (f32)(index / 18) }};
}}

static const u8 OFFSET_LOOKUP[] = {{ {wall_connection_lookup} }};
v2 appearance_get_wall_sprite_coords(Appearance appearance, u8 connection_mask)
{{
	v2 result = {{}};
	result.y = (f32)(appearance - {min_wall_appearance});
	result.x = 9.0f + (f32)OFFSET_LOOKUP[connection_mask];
	return result;
}}

u8 appearance_get_wall_id(Appearance appearance)
{{
	return (u8)(appearance - {min_wall_appearance} + 1);
}}

Appearance appearance_wall_id_to_appearance(u8 wall_id)
{{
	return (Appearance)(wall_id + {min_wall_appearance} - 1);
}}

static const v2 FLOOR_SPRITE_COORDS_LOOKUP[] = {{
	{floor_sprite_coords}
}};
v2 appearance_get_floor_sprite_coords(Appearance appearance)
{{
	return FLOOR_SPRITE_COORDS_LOOKUP[appearance - {min_floor_appearance}];
}}

u8 appearance_get_floor_id(Appearance appearance)
{{
	return (u8)(appearance - {min_floor_appearance} + 1);
}}

Appearance appearance_floor_id_to_appearance(u8 floor_id)
{{
	return (Appearance)(floor_id + {min_floor_appearance} - 1);
}}

static const v2 DOOR_SPRITE_COORDS_LOOKUP[] = {{
	{door_sprite_coords}
}};
v2 appearance_get_door_sprite_coords(Appearance appearance)
{{
	return DOOR_SPRITE_COORDS_LOOKUP[appearance - {min_door_appearance}];
}}

static const v2 LIQUID_SPRITE_COORDS_LOOKUP[] = {{
	{liquid_sprite_coords}
}};
v2 appearance_get_liquid_sprite_coords(Appearance appearance)
{{
	return LIQUID_SPRITE_COORDS_LOOKUP[appearance - {min_liquid_appearance}];
}}

u8 appearance_get_liquid_id(Appearance appearance)
{{
	return (u8)(appearance - {min_liquid_appearance} + 1);
}}

static const v2 ITEM_SPRITE_COORDS_LOOKUP[] = {{
	{item_sprite_coords}
}};
v2 appearance_get_item_sprite_coords(Appearance appearance)
{{
	return ITEM_SPRITE_COORDS_LOOKUP[appearance - {min_item_appearance}];
}}
#endif

#endif
'''.strip()

creature_type_names = ['CREATURE_TYPE_' + n.upper().replace(' ', '_') for n in creatures]
creature_names_strings = ['"{}"'.format(n) for n in creatures]

creature_appearance_names = ['APPEARANCE_CREATURE_' + n.upper().replace(' ', '_') for n in creatures]
wall_appearance_names = ['APPEARANCE_WALL_' + n.upper().replace(' ', '_') for n in walls]
floor_appearance_names = ['APPEARANCE_FLOOR_' + n[0].upper().replace(' ', '_') for n in floors]
door_appearance_names = ['APPEARANCE_DOOR_' + n[0].upper().replace(' ', '_') for n in doors]
liquid_appearance_names = ['APPEARANCE_LIQUID_' + n[0].upper().replace(' ', '_') for n in liquids]
item_appearance_names = ['APPEARANCE_ITEM_' + n[0].upper().replace(' ', '_') for n in items]

output = output_template.format(
	creature_type_names=',\n\t'.join(creature_type_names),
	creature_names_strings=',\n\t'.join(creature_names_strings),

	creature_appearance_names=',\n\t'.join(creature_appearance_names),
	min_creature_appearance=creature_appearance_names[0],
	max_creature_appearance=creature_appearance_names[-1],

	wall_appearance_names=',\n\t'.join(wall_appearance_names),
	min_wall_appearance=wall_appearance_names[0],
	max_wall_appearance=wall_appearance_names[-1],
	wall_connection_lookup=', '.join(map(str, connect_order_lookup)),

	floor_appearance_names=',\n\t'.join(floor_appearance_names),
	min_floor_appearance=floor_appearance_names[0],
	max_floor_appearance=floor_appearance_names[-1],
	floor_sprite_coords=',\n\t'.join(['{{ {0}.0f, {1}.0f }}'.format(f[1][0][0], f[1][0][1]) for f in floors]),

	door_appearance_names=',\n\t'.join(door_appearance_names),
	min_door_appearance=door_appearance_names[0],
	max_door_appearance=door_appearance_names[-1],
	door_sprite_coords=',\n\t'.join(['{{ {0}.0f, {1}.0f }}'.format(d[1][0], d[1][1]) for d in doors]),

	liquid_appearance_names=',\n\t'.join(liquid_appearance_names),
	min_liquid_appearance=liquid_appearance_names[0],
	max_liquid_appearance=liquid_appearance_names[-1],
	liquid_sprite_coords=',\n\t'.join(['{{ {0}.0f, {1}.0f }}'.format(l[1][0], l[1][1]) for l in liquids]),

	item_appearance_names=',\n\t'.join(item_appearance_names),
	min_item_appearance=item_appearance_names[0],
	max_item_appearance=item_appearance_names[-1],
	item_sprite_coords=',\n\t'.join(['{{ {0}.0f, {1}.0f }}'.format(i[1][0], i[1][1]) for i in items]),
)

with open(out_filename, 'w') as outfile:
	outfile.write(output)

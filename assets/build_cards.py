#!/usr/bin/python3

import os
import os.path
import sys
from PIL import Image
import argparse

os.chdir(os.path.dirname(sys.argv[0]))

parser = argparse.ArgumentParser(description='Build cards')
parser.add_argument(
	'-o', '--output',
	metavar='output_filename',
	type=str,
	nargs=1,
	required=True,
	help='Filename of output image.',
)
# parser.add_argument(
# 	'-u', '--output-header',
# 	metavar='output_header',
# 	type=str,
# 	nargs=1,
# 	required=True,
# 	help='Filename of output header',
# )

args = parser.parse_args()
out_filename = args.output[0]
# out_header = args.output_header[0]

available_letters = 'abcdefghijlmnoprstuvw'
blank_card_file = 'cards/blank-card.png'
card_back_file = 'cards/card-back.png'
icons_file = 'cards/icons.png'
icons_2_file = 'cards/icons-2.png'
letters_file = 'cards/letters.png'

blank_card = Image.open(blank_card_file)
card_back = Image.open(card_back_file)
icons = Image.open(icons_file)
icons_2 = Image.open(icons_2_file)
letters = Image.open(letters_file)

letter_locations = {
	'A': ( 1,  1, 3, 4),
	'B': ( 6,  1, 3, 4),
	'C': (11,  1, 3, 4),
	'D': (16,  1, 3, 4),
	'E': (21,  1, 3, 4),

	'F': ( 1,  6, 2, 4),
	'G': ( 6,  6, 3, 4),
	'H': (11,  6, 3, 4),
	'I': (16,  6, 1, 4),
	'J': (21,  6, 2, 4),

	'K': ( 1, 11, 3, 4),
	'L': ( 6, 11, 2, 4),
	'M': (11, 11, 4, 4),
	'N': (16, 11, 3, 4),
	'O': (21, 11, 3, 4),

	'P': ( 1, 16, 3, 4),
	'Q': ( 6, 16, 3, 4),
	'R': (11, 16, 3, 4),
	'S': (16, 16, 2, 4),
	'T': (21, 16, 3, 4),

	'U': ( 1, 21, 3, 4),
	'V': ( 6, 21, 3, 4),
	'W': (11, 21, 5, 4),
	'X': (16, 21, 3, 4),
	'Y': (21, 21, 3, 4),

	'Z': ( 1, 26, 3, 4),
}

# name, sprite_coords
cards = [
	('fire mana',     (0, 0), icons),
	('rock mana',     (1, 0), icons),
	('electric mana', (2, 0), icons),
	('air mana',      (3, 0), icons),
	('earth mana',    (4, 0), icons),
	('psy mana',      (5, 0), icons),
	('water mana',    (6, 0), icons),

	('fireball',       (0, 2), icons),
	('fire shield',    (2, 2), icons),
	('fire wall',      (3, 2), icons),
	('fire bolt',      (1, 2), icons),

	('exchange',       (1, 11), icons),
	('blink',          (7, 13), icons),
	('poison',         (6,  7), icons_2),
	('magic missile',  (8, 10), icons),
	('lightning',      (2, 4), icons),
	('disease',        (9, 24), icons_2),
]

num_card_faces = len(cards) + 1

card_width, card_height = blank_card.size
width, height = num_card_faces * card_width, card_height

output = Image.new('RGBA', (width, height), (0, 0, 0, 0))

def draw_text(text, top_left):
	width = 0
	for c in text.upper():
		if c == ' ':
			width += 3
		else:
			width += letter_locations[c][2]
	width += len(text) - 1
	x, y = top_left[0] + (card_width - width) // 2, top_left[1] + 48
	for c in text.upper():
		if c == ' ':
			x += 4
			continue
		cx, cy, cw, ch = letter_locations[c]
		for j in range(ch):
			for i in range(cw):
				output.putpixel((x + i, y + j), letters.getpixel((cx + i, cy + j)))
		x += cw + 1

for y in range(card_height):
	for x in range(card_width):
		output.putpixel((x, y), card_back.getpixel((x, y)))

for n, card in enumerate(cards, 1):
	text, (icon_x, icon_y), icon_image = card

	top_left_x, top_left_y = n * card_width, 0

	# make blank card base for card
	for y in range(card_height):
		for x in range(card_width):
			output.putpixel((top_left_x + x, top_left_y + y), blank_card.getpixel((x, y)))

	# do text
	draw_text(text, (top_left_x, top_left_y))

	# draw icon
	icon_output_x, icon_output_y = top_left_x + 8, top_left_y + 8
	icon_input_x, icon_input_y = icon_x * 32, icon_y * 32
	for y in range(32):
		for x in range(32):
			output.putpixel((icon_output_x + x, icon_output_y + y),
			                icon_image.getpixel((icon_input_x + x, icon_input_y + y)))

# output.save(out_filename)
pixel_data = []
output_width, output_height = output.size
for y in range(output_height):
	for x in range(output_width):
		r, g, b, a = output.getpixel((x, y))
		val = '0x{:02X}{:02X}{:02X}{:02X}'.format(a, b, g, r)
		pixel_data.append(val)

new_pixel_data = []
for n in range(0, len(pixel_data), 8):
	new_pixel_data.append(', '.join(pixel_data[n:n + 8]))
pixel_data = ',\n\t'.join(new_pixel_data)

card_names_pascal=[n.title().replace(' ', '_') for (n, _, __) in cards]

output_template = '''
#ifndef CARD_DATA_H
#define CARD_DATA_H

#include "prelude.h"

enum Card_Appearance
{{
	{card_names_upper},
	NUM_CARD_APPEARANCES
}};

const v2_u32 CARD_IMAGE_SIZE = {{ {image_width}, {image_height} }};
extern u32 CARD_IMAGE_DATA[{num_pixels}];
v2 card_appearance_get_sprite_coords(Card_Appearance card_appearance);

#ifndef JFG_HEADER_ONLY
u32 CARD_IMAGE_DATA[{num_pixels}] = {{
	{pixel_data}
}};

v2 card_appearance_get_sprite_coords(Card_Appearance card_appearance)
{{
	return {{ (f32)(card_appearance + 1), 0.0f }};
}}
#endif

#endif
'''.strip()

with open(out_filename, 'w') as output_header:
	output_header.write(output_template.format(
		card_names_upper=',\n\t'.join(['CARD_APPEARANCE_{}'.format(n.upper()) for n in card_names_pascal]),
		num_pixels=(card_width * card_height * (len(cards) + 1)),
		pixel_data=pixel_data,
		image_width=output_width,
		image_height=output_height,
	))

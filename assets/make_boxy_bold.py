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
	help='Filename of output header.',
)
parser.add_argument(
	'-i', '--input',
	metavar='input_filename',
	type=str,
	nargs=1,
	required=True,
	help='Filename of input image.',
)

args = parser.parse_args()
out_filename = args.output[0]
in_filename = args.input[0]

input_image = Image.open(in_filename)

BOXY_HEIGHT = 9
BOXY_SPACE_WIDTH = 3
BOXY_RECT_DICT = {
	'!':  (  1,  0,  4, 9), '"':  (  6,  0,  7, 9), '#':  ( 14,  0,  9, 9),
	'$':  ( 24,  0,  7, 9), '%':  ( 32,  0, 10, 9), '&':  ( 43,  0,  9, 9),
	"'":  ( 53,  0,  4, 9), '(':  ( 58,  0,  5, 9), ')':  ( 64,  0,  5, 9),
	'*':  ( 70,  0,  6, 9), '+':  ( 77,  0,  8, 9), ',':  ( 86,  0,  5, 9),
	'-':  ( 92,  0,  6, 9), '.':  ( 99,  0,  4, 9), '/':  (104,  0,  6, 9),
	'0':  (  1,  9,  7, 9), '1':  (  9,  9,  4, 9), '2':  ( 14,  9,  7, 9),
	'3':  ( 22,  9,  7, 9), '4':  ( 30,  9,  7, 9), '5':  ( 38,  9,  7, 9),
	'6':  ( 46,  9,  7, 9), '7':  ( 54,  9,  7, 9), '8':  ( 62,  9,  7, 9),
	'9':  ( 70,  9,  7, 9), ':':  (  1, 18,  4, 9), ';':  (  6, 18,  4, 9),
	'<':  ( 11, 18,  6, 9), '=':  ( 18, 18,  6, 9), '>':  ( 25, 18,  6, 9),
	'?':  ( 32, 18,  8, 9), '@':  ( 41, 18,  8, 9), 'A':  (  1, 27,  7, 9),
	'B':  (  9, 27,  7, 9), 'C':  ( 17, 27,  7, 9), 'D':  ( 25, 27,  7, 9),
	'E':  ( 33, 27,  7, 9), 'F':  ( 41, 27,  7, 9), 'G':  ( 49, 27,  7, 9),
	'H':  ( 57, 27,  7, 9), 'I':  ( 65, 27,  4, 9), 'J':  ( 70, 27,  7, 9),
	'K':  ( 78, 27,  7, 9), 'L':  ( 86, 27,  7, 9), 'M':  ( 94, 27,  9, 9),
	'N':  (  1, 36,  8, 9), 'O':  ( 10, 36,  7, 9), 'P':  ( 18, 36,  7, 9),
	'Q':  ( 26, 36,  8, 9), 'R':  ( 35, 36,  7, 9), 'S':  ( 43, 36,  7, 9),
	'T':  ( 51, 36,  8, 9), 'U':  ( 60, 36,  7, 9), 'V':  ( 68, 36,  7, 9),
	'W':  ( 76, 36,  9, 9), 'X':  ( 86, 36,  7, 9), 'Y':  ( 94, 36,  8, 9),
	'Z':  (103, 36,  7, 9), '[':  (  1, 45,  5, 9), '\\': (  7, 45,  6, 9),
	']':  ( 14, 45,  5, 9), '^':  ( 20, 45,  8, 9), '_':  ( 29, 45,  6, 9),
	'`':  ( 36, 45,  5, 9), 'a':  (  1, 56,  7, 9), 'b':  (  9, 56,  7, 9),
	'c':  ( 17, 56,  6, 9), 'd':  ( 24, 56,  7, 9), 'e':  ( 32, 56,  7, 9),
	'f':  ( 40, 56,  6, 9), 'g':  ( 47, 56,  7, 9), 'h':  ( 55, 56,  7, 9),
	'i':  ( 63, 56,  5, 9), 'j':  ( 69, 56,  6, 9), 'k':  ( 76, 56,  7, 9),
	'l':  ( 84, 56,  5, 9), 'm':  ( 90, 56,  9, 9), 'n':  (100, 56,  7, 9),
	'o':  (  1, 65,  7, 9), 'p':  (  9, 65,  7, 9), 'q':  ( 17, 65,  7, 9),
	'r':  ( 25, 65,  7, 9), 's':  ( 33, 65,  7, 9), 't':  ( 41, 65,  6, 9),
	'u':  ( 48, 65,  7, 9), 'v':  ( 56, 65,  7, 9), 'w':  ( 64, 65,  9, 9),
	'x':  ( 74, 65,  7, 9), 'y':  ( 82, 65,  7, 9), 'z':  ( 90, 65,  7, 9),
	'{':  ( 42, 45,  5, 9), '|':  ( 48, 45,  4, 9), '}':  ( 53, 45,  5, 9),
	'~':  ( 59, 45,  9, 9),
}

glyph_data = []
for i in range(256):
	c = chr(i)
	x, y, w, h = BOXY_RECT_DICT['?']
	if c in BOXY_RECT_DICT:
		x, y, w, h = BOXY_RECT_DICT[c]
	glyph_data.append((x, y, w, h))

pixel_data = []
width, height = input_image.size
for y in range(height):
	for x in range(width):
		r, g, b, a = input_image.getpixel((x, y))
		val = '0x{:02X}{:02X}{:02X}{:02X}'.format(a, b, g, r)
		pixel_data.append(val)

new_pixel_data = []
for n in range(0, len(pixel_data), 8):
	new_pixel_data.append(', '.join(pixel_data[n:n + 8]))
pixel_data_str = ',\n\t'.join(new_pixel_data)

template = '''
#ifndef BOXY_BOLD_GLYPH_POSS_H
#define BOXY_BOLD_GLYPH_POSS_H

#include "prelude.h"

struct Boxy_Bold_Glyph_Pos
{{
	v2_u8 top_left;
	v2_u8 dimensions;
}};

extern v2_u32 boxy_bold_texture_size;
extern u32 boxy_bold_pixel_data[{pixel_data_len}];
extern Boxy_Bold_Glyph_Pos boxy_bold_glyph_poss[256];

#ifndef JFG_HEADER_ONLY
v2_u32 boxy_bold_texture_size = {{ {width}, {height} }};

Boxy_Bold_Glyph_Pos boxy_bold_glyph_poss[256] = {{
	{glyph_poss}
}};

u32 boxy_bold_pixel_data[{pixel_data_len}] = {{
	{pixel_data}
}};
#endif

#endif
'''.strip()

glyph_poss = ['{{ {{ {0}, {1} }}, {{ {2}, {3} }} }}'.format(x, y, w, h) for x, y, w, h, in glyph_data]
glyph_poss_str = []
for n in range(0, len(glyph_poss), 8):
	glyph_poss_str.append(', '.join(glyph_poss[n:n+8]))
glyph_poss_str = ',\n\t'.join(glyph_poss_str)

output = template.format(
	width=width,
	height=height,
	glyph_poss=glyph_poss_str,
	pixel_data_len=len(pixel_data),
	pixel_data=pixel_data_str,
)

with open(out_filename, 'w') as file:
	file.write(output)

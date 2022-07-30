#!/usr/bin/python2

import os
import os.path
import shutil
import sys
from PIL import Image
import argparse

# os.chdir(os.path.dirname(sys.argv[0]))

parser = argparse.ArgumentParser(description='Build cards')
parser.add_argument(
	'-o', '--output-dir',
	metavar='output_dir',
	type=str,
	nargs=1,
	required=True,
	help='Directory to output wave files.',
)
parser.add_argument(
	'-i', '--input-dir',
	metavar='input_dir',
	type=str,
	nargs=1,
	required=True,
	help='Directory of input wave files.',
)

args = parser.parse_args()
in_dir = args.input_dir[0]
out_dir = args.output_dir[0]

def copy_waves_from_dir(dir):
	for filename in os.listdir(dir):
		if filename.startswith('.'):
			continue
		filename = os.path.join(dir, filename)
		if os.path.isdir(filename):
			copy_waves_from_dir(filename)
		elif os.path.isfile(filename):
			filepath, orig_filename = os.path.split(filename)
			new_filename = orig_filename \
				.replace(' ', '_') \
				.replace('-', '_') \
				.replace('__', '_') \
				.replace('__', '_') \
				.replace('__', '_') \
				.replace('__', '_') \
				.replace('__', '_') \
				.replace('(', '') \
				.replace(')', '') \
				.lower()
			if new_filename.endswith('.wav'):
				print('Copying "{}"'.format(filename))
				shutil.copy(
					os.path.join(filepath, orig_filename),
					os.path.join(out_dir, new_filename),
				)

copy_waves_from_dir(in_dir)

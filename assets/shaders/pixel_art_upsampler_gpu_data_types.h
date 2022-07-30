#ifndef PIXEL_ART_UPSAMPLER_GPU_DATA_TYPES_H
#define PIXEL_ART_UPSAMPLER_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

#define PIXEL_ART_UPSAMPLER_WIDTH  8
#define PIXEL_ART_UPSAMPLER_HEIGHT 8

struct Pixel_Art_Upsampler_Constant_Buffer
{
	F2 input_size;
	F2 output_size;

	F2 input_offset;
	F2 output_offset;
};

#endif

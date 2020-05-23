#ifndef PIXEL_ART_UPSAMPLER_GPU_DATA_TYPES_H
#define PIXEL_ART_UPSAMPLER_GPU_DATA_TYPES_H

#include "jfg/cpu_gpu_data_types.h"

#define PIXEL_ART_UPSAMPLER_WIDTH  8
#define PIXEL_ART_UPSAMPLER_HEIGHT 8

struct Pixel_Art_Upsampler_Constant_Buffer
{
	U2 input_size;
	U2 output_size;

	U2 input_offset;
	U2 output_offset;
};

#endif

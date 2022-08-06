#pragma once

#include "prelude.h"
#include "platform_functions.h"

struct Texture
{
	v2_u32 size;
	Color  data[];
};

Texture* load_texture(const char* filename, Platform_Functions* platform_functions);
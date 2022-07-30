#pragma once

#include "prelude.h"

#include "platform_functions.h"

#define ASSETS_FILE_NAME "assets.bin"
#define ASSETS_DATA_MAX_SIZE (1024*1024*1024)

enum Sound_ID
{
#define SOUND(name) SOUND_##name,
#include "sounds.list.h"
#undef SOUND
	NUM_SOUNDS
};

struct Sound_Header
{
	u8  num_channels;
	u8  sample_width;
	u32 sample_rate;
	u32 num_samples;
};

enum Asset_ID
{
#define SOUND(name) ASSET_SOUND_##name,
#include "sounds.list.h"
#undef SOUND
	NUM_ASSETS
};

static inline Asset_ID sound_id_to_asset_id(Sound_ID sound_id)
{
	return (Asset_ID)sound_id;
}

struct Asset_Entry
{
	u32   file_offset;
	u32   size;
	void *data;
};

struct Assets_Header
{
	Sound_Header sound_headers[NUM_SOUNDS];
	Asset_Entry  asset_entries[NUM_ASSETS];
};

u8 try_load_assets(Platform_Functions* platform_functions, Assets_Header* header);
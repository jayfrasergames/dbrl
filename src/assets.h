#ifndef ASSETS_H
#define ASSETS_H

#include "jfg/prelude.h"
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

#ifndef JFG_HEADER_ONLY
u8 try_load_assets(Platform_Functions* platform_functions, Assets_Header* header)
{
	u8 succeeded = platform_functions->try_read_file(ASSETS_FILE_NAME,
	                                                 header,
	                                                 sizeof(Assets_Header) + ASSETS_DATA_MAX_SIZE);
	if (!succeeded) {
		return 0;
	}
	u8 *data = (u8*)header;
	for (u32 i = 0; i < NUM_ASSETS; ++i) {
		Asset_Entry *ae = &header->asset_entries[i];
		ae->data = data + ae->file_offset;
	}
	return 1;
}
#endif

#endif

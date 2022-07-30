#include "assets.h"

#include "stdafx.h"

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
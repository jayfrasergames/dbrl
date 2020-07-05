#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

#define MIN_ALIGNMENT 16
#define SOUND_DIR "..\\assets\\sound\\"

struct Sound_Metadata
{
	const char *name;
};

Sound_Metadata SOUND_METADATA[] = {
#define SOUND(name) { #name },
#include "sounds.list.h"
#undef SOUND
};

void *sound_data[NUM_SOUNDS] = {};

struct Chunk_Header
{
	char name[4];
	u32 size;
};

static u8 compare_chunk_header_name(Chunk_Header ch, const char *name)
{
	ASSERT(strlen(name) == 4);
	for (const char *p = ch.name, *q = name; *q; ++p, ++q) {
		if (*p != *q) {
			return 0;
		}
	}
	return 1;
}

struct Wave_Header
{
	Chunk_Header chunk_header;
	u32 format;
};

struct Wave_Format_Header
{
	u16 audio_format;
	u16 num_channels;
	u32 sample_rate;
	u32 byte_rate;
	u16 block_align;
	u16 bits_per_sample;
};

u32 text_to_u32(const char *text)
{
	ASSERT(strlen(text) == 4);
	u32 result;
	for (char *s = (char*)text, *d = (char*)&result; *s; ++s, ++d) {
		*d = *s;
	}
	return result;
}

void *load_wave(const char *name)
{
	char filename[4096];
	strcpy(filename, SOUND_DIR);
	{
		char *p = filename + strlen(filename);
		strcat(filename, name);
		for ( ; *p; ++p) {
			char c = *p;
			if (c >= 'A' && c <= 'Z') {
				c -= 'A' - 'a';
				*p = c;
			}
		}
	}
	strcat(filename, ".wav");

	// printf("Reading sound: %s\n", name);

	FILE *fp = fopen(filename, "rb");
	ASSERT(fp);

	int seek_failed = fseek(fp, 0, SEEK_END);
	ASSERT(!seek_failed);
	size_t filesize = ftell(fp);

	seek_failed = fseek(fp, 0, SEEK_SET);
	ASSERT(!seek_failed);

	Wave_Header header = {};
	size_t read_succeeded = fread(&header, sizeof(header), 1, fp);
	ASSERT(read_succeeded);

	ASSERT(compare_chunk_header_name(header.chunk_header, "RIFF"));
	ASSERT(header.format == text_to_u32("WAVE"));

	u8 format_read = 0;
	u8 data_read = 0;
	Wave_Format_Header format = {};

	void *result = NULL;
	while (ftell(fp) < filesize) {
		Chunk_Header chunk_header;
		read_succeeded = fread(&chunk_header, sizeof(chunk_header), 1, fp);
		if (!read_succeeded && data_read) {
			break;
		}
		ASSERT(read_succeeded);

		if (compare_chunk_header_name(chunk_header, "fmt ")) {
			ASSERT(chunk_header.size >= sizeof(format));
			read_succeeded = fread(&format, sizeof(format), 1, fp);
			ASSERT(format.audio_format == 1); // PCM
			u32 advance_amount = chunk_header.size - sizeof(format);
			if (advance_amount) {
				fseek(fp, advance_amount, SEEK_CUR);
			}
			format_read = 1;
		} else if (compare_chunk_header_name(chunk_header, "data")) {
			ASSERT(format_read && !data_read);
			result = malloc(chunk_header.size + sizeof(Sound_Header));

			Sound_Header *output_header = (Sound_Header*)result;
			output_header->num_channels = (u8)format.num_channels;
			output_header->sample_width = (u8)format.bits_per_sample / 8;
			output_header->sample_rate = format.sample_rate;
			u32 sample_size = format.num_channels * format.bits_per_sample / 8;
			output_header->num_samples = chunk_header.size / sample_size;

			void *data = (void*)((u8*)result + sizeof(*output_header));

			read_succeeded = fread(data, chunk_header.size, 1, fp);
			ASSERT(read_succeeded);
			data_read = 1;
		} else {
			/*
			printf("Unrecognised chunk type: \"%c%c%c%c\"\n",
			       chunk_header.name[0],
			       chunk_header.name[1],
			       chunk_header.name[2],
			       chunk_header.name[3]);
			*/
			seek_failed = fseek(fp, chunk_header.size, SEEK_CUR);
		}
	}
	ASSERT(result);

	int close_failed = fclose(fp);
	ASSERT(!close_failed);

	return result;
}

Assets_Header header;

void write_asset(FILE *fp, u32 size, void *data, u32 *cur_offset, Asset_ID asset_id)
{
	u32 offset = *cur_offset;
	offset += MIN_ALIGNMENT - 1;
	offset &= ~(MIN_ALIGNMENT - 1);
	u32 advance_required = offset - *cur_offset;
	for (u32 i = 0; i < advance_required; ++i) {
		fputc(0, fp);
	}

	size_t written = fwrite(data, size, 1, fp);
	ASSERT(written);

	Asset_Entry entry = {};
	entry.file_offset = offset;
	entry.size = size;
	entry.data = NULL;

	header.asset_entries[asset_id] = entry;

	offset += size;
	*cur_offset = offset;
}

int main(int argc, char **argv)
{
	for (u32 i = 0; i < NUM_SOUNDS; ++i) {
		sound_data[i] = load_wave(SOUND_METADATA[i].name);
	}

	FILE *fp = fopen(ASSETS_FILE_NAME, "wb");
	ASSERT(fp);

	u32 cur_offset = sizeof(header);
	int seek_failed = fseek(fp, cur_offset, SEEK_SET);
	ASSERT(!seek_failed);

	for (u32 i = 0; i < NUM_SOUNDS; ++i) {
		Sound_Header *dst = &header.sound_headers[i];
		Sound_Header *src = (Sound_Header*)sound_data[i];

		memcpy(dst, src, sizeof(Sound_Header));
		u32 size = src->num_channels * src->sample_width * src->num_samples;
		void *data = (u8*)src + sizeof(*src);
		write_asset(fp, size, data, &cur_offset, sound_id_to_asset_id((Sound_ID)i));
	}

	seek_failed = fseek(fp, 0, SEEK_SET);
	ASSERT(!seek_failed);
	size_t written = fwrite(&header, sizeof(header), 1, fp);
	ASSERT(written);

	fclose(fp);

	return EXIT_SUCCESS;
}

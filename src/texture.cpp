#include "texture.h"

#include "stdafx.h"

#define TEXTURE_DIR "..\\assets\\textures"
// XXX -- get a temp buffer
#define TMP_BUFFER_SIZE 10*1024*1024

struct Buffer_Read_Info
{
	void   *buffer;
	size_t  cur_pos;
};

extern "C"
static void custom_png_read_fn(png_structp png_ptr, png_bytep data, png_size_t length)
{
	// should load/read entire file _here_
	// png_error() if we fail to read the file
	// png_voidp readp = png_get_io_ptr(png_ptr);
	auto buffer_read_info = (Buffer_Read_Info*)png_get_io_ptr(png_ptr);
	memcpy(data, (void*)((uptr)buffer_read_info->buffer + buffer_read_info->cur_pos), length);
	buffer_read_info->cur_pos += length;
}

Texture* load_texture(const char* filename, Platform_Functions* platform_functions)
{
	Texture *result = NULL;

	void *tmp_buffer = malloc(TMP_BUFFER_SIZE);

	char *full_filename = fmt("%s\\%s", TEXTURE_DIR, filename);
	if (!platform_functions->try_read_file(full_filename, tmp_buffer, TMP_BUFFER_SIZE)) {
		goto cleanup_tmp_buffer;
	}

	#define PNG_SIG_SIZE 8
	if (!png_check_sig((png_const_bytep)tmp_buffer, PNG_SIG_SIZE)) {
		goto cleanup_tmp_buffer;
	}

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		goto cleanup_tmp_buffer;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		goto cleanup_png_ptr;
	}

	png_infop end_info = png_create_info_struct(png_ptr);
	if (!end_info) {
		goto cleanup_png_ptr;
	}

	jmp_buf *err_jmp_buf_ptr = png_set_longjmp_fn(png_ptr, longjmp, sizeof(jmp_buf));
	jmp_buf err_jmp_buf;
	memcpy(&err_jmp_buf, err_jmp_buf_ptr, sizeof(err_jmp_buf));
	if (setjmp(err_jmp_buf)) {
		goto cleanup_png_ptr;
	}

	Buffer_Read_Info buffer_read_info = {};
	buffer_read_info.buffer = tmp_buffer;
	buffer_read_info.cur_pos = 0;
	png_set_read_fn(png_ptr, &buffer_read_info, custom_png_read_fn);

	png_read_png(png_ptr, info_ptr, 0, NULL);

	v2_u32 image_size = v2_u32(png_get_image_width(png_ptr, info_ptr), png_get_image_height(png_ptr, info_ptr));

	result = (Texture*)malloc(sizeof(*result) + image_size.w * image_size.h * sizeof(Color));
	if (!result) {
		goto cleanup_png_ptr;
	}
	result->size = image_size;

	void *write_ptr = result + 1;
	// copy image data
	auto row_pointers = png_get_rows(png_ptr, info_ptr);
	for (u32 i = 0; i < image_size.h; ++i) {
		auto row = (Color*)row_pointers[i];
		size_t row_size = sizeof(*row) * image_size.w;
		memcpy(write_ptr, row, image_size.w * sizeof(*row));
		write_ptr = (void*)((uptr)write_ptr + row_size);
	}

cleanup_png_ptr:
	png_destroy_read_struct(&png_ptr, NULL, NULL);
cleanup_tmp_buffer:
	free(tmp_buffer);

	return result;
}
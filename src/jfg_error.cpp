#include "jfg_error.h"

#include "stdafx.h"

thread_local bool is_error = false;
thread_local char error_buffer[4096] = {};

void jfg_set_error(const char* format_string, ...)
{
	va_list args;
	va_start(args, format_string);
	vsnprintf(error_buffer, ARRAY_SIZE(error_buffer), format_string, args);
	va_end(args);

	is_error = true;
}

char* jfg_get_error()
{
	if (is_error) {
		return error_buffer;
	}
	return NULL;
}

void jfg_clear_error()
{
	is_error = false;
}
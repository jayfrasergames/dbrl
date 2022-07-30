#include "format.h"

#include "stdafx.h"

char *fmt(const char* fmtstr, ...)
{
	static thread_local char buffer[4096];
	va_list args;
	va_start(args, fmtstr);
	vsnprintf(buffer, ARRAY_SIZE(buffer), fmtstr, args);
	va_end(args);
	return buffer;
}
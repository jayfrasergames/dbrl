#pragma once

// #include "log.h"

enum JFG_Error
{
	JFG_SUCCESS,
	JFG_ERROR,
};

void jfg_clear_error();
void jfg_set_error(const char* format_string, ...);
char* jfg_get_error();
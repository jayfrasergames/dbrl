#include "jfg_dsound.h"

#define DSOUND_FUNCTION(return_type, name, ...) return_type (WINAPI *name)(__VA_ARGS__) = NULL;
DSOUND_FUNCTIONS
#undef DSOUND_FUNCTION

JFG_Error dsound_try_load()
{
	HMODULE dsound_library = LoadLibraryA("dsound.dll");
	if (!dsound_library) {
		jfg_set_error("Failed to load \"dsound.dll\"!");
		return JFG_ERROR;
	}
#define DSOUND_FUNCTION(return_type, name, ...) \
		name = (return_type (WINAPI *)(__VA_ARGS__))GetProcAddress(dsound_library, #name);
	DSOUND_FUNCTIONS
#undef DSOUND_FUNCTION
	return JFG_SUCCESS;
}
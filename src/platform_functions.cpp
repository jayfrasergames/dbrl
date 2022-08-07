#include "platform_functions.h"

#define PLATFORM_FUNCTION(return_type, name, ...) return_type (*name)(__VA_ARGS__) = NULL;
	PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION
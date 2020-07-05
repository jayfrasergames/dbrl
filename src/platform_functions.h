#ifndef PLATFORM_FUNCTIONS_H
#define PLATFORM_FUNCTIONS_H

typedef void (*Thread_Function)(void* data);

#define PLATFORM_FUNCTIONS \
	PLATFORM_FUNCTION(void, start_thread, Thread_Function thread_function, void* thread_args) \
	PLATFORM_FUNCTION(void, sleep, u32 time_in_milliseconds) \
	PLATFORM_FUNCTION(u32, try_read_file, char* filename, void* dest, u32 max_size)

struct Platform_Functions
{
#define PLATFORM_FUNCTION(return_type, name, ...) return_type (*name)(__VA_ARGS__);
	PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION
};

#endif

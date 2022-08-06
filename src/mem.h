#ifndef JFG_MEMORY_H
#define JFG_MEMORY_H

#include "prelude.h"

struct Memory_Spec
{
	size_t alignment;
	size_t size;
};

inline uptr align(uptr ptr, uptr alignment)
{
	return (ptr + alignment - 1) & ~(alignment - 1);
}

// TODO -- allocators, global context etc

#endif

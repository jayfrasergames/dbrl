#ifndef JFG_RANDOM_H
#define JFG_RANDOM_H

#include "prelude.h"

struct MT19937
{
	u32 idx;
	u32 x[624];

	void seed(u32 seed);
	u32  rand_u32();
	f32  rand_f32();
	f32  uniform_f32(f32 start, f32 end);
	void set_current();
};

extern thread_local u32 (*rand_u32)();
extern thread_local f32 (*rand_f32)();
extern thread_local f32 (*uniform_f32)(f32 start, f32 end);

#endif

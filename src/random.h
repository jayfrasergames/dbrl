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

#ifndef JFG_HEADER_ONLY

static thread_local void *random_cur_state = NULL;
thread_local u32 (*rand_u32)() = NULL;
thread_local f32 (*rand_f32)() = NULL;
thread_local f32 (*uniform_f32)(f32 start, f32 end) = NULL;

static u32 mt19937_rand_u32()
{
	ASSERT(random_cur_state);
	MT19937 *rand = (MT19937*)random_cur_state;
	return rand->rand_u32();
}

static f32 mt19937_rand_f32()
{
	ASSERT(random_cur_state);
	MT19937 *rand = (MT19937*)random_cur_state;
	return rand->rand_f32();
}

static f32 mt19937_uniform_f32(f32 start, f32 end)
{
	ASSERT(random_cur_state);
	MT19937 *rand = (MT19937*)random_cur_state;
	return rand->uniform_f32(start, end);
}

void MT19937::set_current()
{
	random_cur_state = this;
	::rand_u32 = mt19937_rand_u32;
	::rand_f32 = mt19937_rand_f32;
	::uniform_f32 = mt19937_uniform_f32;
}

static void mt19937_twist(MT19937* mt19937)
{
	const u32 upper_mask = 0x80000000;
	const u32 lower_mask = 0x7FFFFFFF;
	const u32 a = 0x9908B0DF;
	const u32 n = ARRAY_SIZE(mt19937->x);
	const u32 m = 312;
	u32 *x = mt19937->x;
	for (u32 i = 0; i < n; ++i) {
		u32 tmp = (x[i] & upper_mask) | (x[(i + 1) % n] & lower_mask);
		u32 xA = (tmp >> 1) ^ ((tmp & 1) * a);
		x[i] = x[(i + m) % n] ^ xA;
	}
}

void MT19937::seed(u32 seed)
{
	x[0] = seed;
	const u32 f = 1812433253;
	for (u32 i = 1; i < ARRAY_SIZE(x); ++i) {
		seed = f * (seed ^ (seed >> 30)) + i;
		x[i] = seed;
	}
	mt19937_twist(this);
	idx = 0;
}

u32 MT19937::rand_u32()
{
	const u32 b = 0x9D2C5680;
	const u32 c = 0xEFC60000;
	const u32 d = 0xFFFFFFFF;
	const u32 s = 7;
	const u32 t = 15;
	const u32 u = 11;
	const u32 l = 18;

	if (idx == ARRAY_SIZE(x)) {
		mt19937_twist(this);
		idx = 0;
	}
	u32 y = x[idx++];
	y = y ^ ((y >> u) & d);
	y = y ^ ((y << s) & b);
	y = y ^ ((y << t) & c);
	y = y ^ (y >> l);

	return y;
}

f32 MT19937::rand_f32()
{
	u32 r = rand_u32();
	return (f32)r / (f32)(u32)-1;
}

f32 MT19937::uniform_f32(f32 start, f32 end)
{
	return start + (end - start)*rand_f32();
}

#endif

#endif

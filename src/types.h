#pragma once

typedef v2_u8 Pos;

static inline u16 pos_to_u16(Pos p)
{
	return p.y * 256 + p.x;
}

static inline Pos u16_to_pos(u16 n)
{
	return Pos(n % 256, n / 256);
}

static inline bool positions_are_adjacent(Pos a, Pos b)
{
	v2_i32 d = (v2_i32)a - (v2_i32)b;
	d *= d;
	return d.x <= 1 && d.y <= 1;
}

template <typename T>
struct Map_Cache
{
	T items[256 * 256];
	T& operator[](Pos pos) { return items[pos.y * 256 + pos.x]; }
};

struct Map_Cache_Bool
{
	u64  items[256 * 256 / 64];
	u64  get(Pos p)   { u16 idx = pos_to_u16(p); return items[idx / 64] & ((u64)1 << (idx % 64)); }
	void set(Pos p)   { u16 idx = pos_to_u16(p); items[idx / 64] |= ((u64)1 << (idx % 64)); }
	void unset(Pos p) { u16 idx = pos_to_u16(p); items[idx / 64] &= ~((u64)1 << (idx % 64)); }
	void reset()      { memset(items, 0, sizeof(items)); }
};

#define MAX_ENTITIES 10240

typedef u32 Entity_ID;

// Forward declarations

struct Render;
struct Log;

// XXX -- should get rid of this
void debug_pause();
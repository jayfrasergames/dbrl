#ifndef TYPES_H
#define TYPES_H

typedef v2_u8 Pos;

u16 pos_to_u16(Pos p)
{
	return p.y * 256 + p.x;
}

Pos u16_to_pos(u16 n)
{
	return V2_u8(n % 256, n / 256);
}

struct Map_Cache_Bool
{
	u64  items[256 * 256 / 64];
	u64  get(Pos p)   { u16 idx = pos_to_u16(p); return items[idx / 64] & ((u64)1 << (idx % 64)); }
	void set(Pos p)   { u16 idx = pos_to_u16(p); items[idx / 64] |= ((u64)1 << (idx % 64)); }
	void unset(Pos p) { u16 idx = pos_to_u16(p); items[idx / 64] &= ~((u64)1 << (idx % 64)); }
	void reset()      { memset(items, 0, sizeof(items)); }
};

#define MAX_ENTITIES 10240

typedef u16 Entity_ID;

#endif

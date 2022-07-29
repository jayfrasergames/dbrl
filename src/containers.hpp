#ifndef JFG_CONTAINERS_H
#define JFG_CONTAINERS_H

#include "prelude.h"

template <typename T>
struct Slice
{
	T  *base;
	u32 len;

	operator bool() { return len; }
	T& operator[](u32 idx) { ASSERT(idx < len); return base[idx]; }

	Slice(T* base, u32 len) : base(base), len(len) { }
};

template <typename T>
Slice<T> slice_one(T* item)
{
	return Slice<T>(item, 1);
}

template <typename T>
struct Output_Buffer
{
	T   *base;
	u32 *len;
	u32 size;

	operator bool() { return *len; }
	T& operator[](u32 idx) { ASSERT(idx < *len); return base[idx]; }

	void reset()        { *len = 0; }
	void append(T item) { ASSERT(*len < size); base[(*len)++] = item; }

	Output_Buffer(T* base, u32* len, u32 size) : base(base), len(len), size(size) { }
};

template <typename T, u32 size>
struct Stack
{
	u32 top;
	T   items[size];

	operator bool()     { return top; }
	operator Slice<T>() { return Slice<T>(items, top); }

	void reset()      { top = 0; }
	void push(T item) { ASSERT(top < size); items[top++] = item; }
	T    peek()       { ASSERT(top); return items[top - 1]; }
	T*   peek_ptr()   { ASSERT(top); return &items[top - 1]; }
	T    pop()        { ASSERT(top); return items[--top]; }
};

template <typename T, u32 size>
struct Max_Length_Array
{
	u32 len;
	T   items[size];

	operator bool()             { return len; }
	operator Slice<T>()         { return Slice<T>(items, len); }
	operator Output_Buffer<T>() { return Output_Buffer<T>(items, &len, size); }
	T& operator[](u32 idx)      { ASSERT(idx < len); return items[idx]; }

	void reset()         { len = 0; }
	void append(T item)  { ASSERT(len < size); items[len++] = item; }
	T    pop()           { ASSERT(len); return items[--len]; }
	void remove(u32 idx) { ASSERT(idx < len); items[idx] = items[--len]; }
	void remove_preserve_order(u32 idx)
	{
		ASSERT(idx < len);
		--len;
		for (u32 i = idx; i < len; ++i) {
			items[i] = items[i + 1];
		}
	}
	Slice<T> slice(u32 start, u32 length)
	{
		ASSERT(start + length < len);
		return Slice<T>(&items[start], length);
	}
};

template <u32 size>
struct Bit_Array
{
	u32 vals[(size + 31) / 32];

	void reset()        { for (u32 i = 0; i < (size + 31) / 32; ++i) vals[i] = 0; }
	void set(u32 idx)   { ASSERT(idx < size); vals[idx / 32] |= (1 << (idx % 32)); }
	void unset(u32 idx) { ASSERT(idx < size); vals[idx / 32] &= ~(1 << (idx % 32)); }
	u32  get(u32 idx)   { ASSERT(idx < size); return vals[idx / 32] & (1 << (idx % 32)); }
};

#endif

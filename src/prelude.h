#ifndef JFG_PRELUDE_H
#define JFG_PRELUDE_H

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef uintptr_t uptr;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef intptr_t iptr;

typedef float  f32;
typedef double f64;

#define ASSERT(expr) assert(expr)
#define ARRAY_SIZE(xs) (sizeof(xs)/sizeof(xs[0]))
#define OFFSET_OF(struct_type, member) ((size_t)(&((struct_type*)0)->member))
#define STATIC_ASSERT(COND, MSG) typedef u8 static_assertion_##MSG[(COND) ? 1 : -1]

#ifdef LIBRARY
	#define LIBRARY_EXPORT extern "C" __declspec(dllexport)
#else
	#define LIBRARY_EXPORT
#endif

#define VEC_TYPES \
	VEC_TYPE(u8) VEC_TYPE(u16) VEC_TYPE(u32) VEC_TYPE(u64) \
	VEC_TYPE(i8) VEC_TYPE(i16) VEC_TYPE(i32) VEC_TYPE(i64) \
	VEC_TYPE(f32) VEC_TYPE(f64)

#define SIGNED_VEC_TYPES \
	VEC_TYPE(i8) VEC_TYPE(i16) VEC_TYPE(i32) VEC_TYPE(i64) \
	VEC_TYPE(f32) VEC_TYPE(f64)

// forward declare vec types
#define VEC_TYPE(type) struct v2_##type; struct v3_##type; struct v4_##type;
VEC_TYPES
#undef VEC_TYPE

// declare vec types
#define VEC_TYPE(type) \
	struct v2_##type \
	{ \
		v2_##type() = default; \
		v2_##type(type a, type b) : x(a), y(b) {} \
		union { type x, w; }; \
		union { type y, h; }; \
		operator bool() { return x || y; } \
		explicit operator v2_u8(); \
		explicit operator v2_u16(); \
		explicit operator v2_u32(); \
		explicit operator v2_u64(); \
		explicit operator v2_i8(); \
		explicit operator v2_i16(); \
		explicit operator v2_i32(); \
		explicit operator v2_i64(); \
		explicit operator v2_f32(); \
		explicit operator v2_f64(); \
	}; \
	struct v3_##type \
	{ \
		v3_##type() = default; \
		v3_##type(type a, type b, type c) : x(a), y(b), z(c) {} \
		union { type x, w, r; }; \
		union { type y, h, g; }; \
		union { type z, d, b; }; \
		operator bool() { return x || y || z; } \
		explicit operator v3_u8(); \
		explicit operator v3_u16(); \
		explicit operator v3_u32(); \
		explicit operator v3_u64(); \
		explicit operator v3_i8(); \
		explicit operator v3_i16(); \
		explicit operator v3_i32(); \
		explicit operator v3_i64(); \
		explicit operator v3_f32(); \
		explicit operator v3_f64(); \
	}; \
	struct v4_##type \
	{ \
		v4_##type() = default; \
		v4_##type(type a, type b, type c, type d) : x(a), y(b), z(c), w(d) {} \
		union { type x, r; }; \
		union { type y, g; }; \
		union { type z, b; }; \
		union { type w, a; }; \
		operator bool() { return x || y || z || w; } \
		explicit operator v4_u8(); \
		explicit operator v4_u16(); \
		explicit operator v4_u32(); \
		explicit operator v4_u64(); \
		explicit operator v4_i8(); \
		explicit operator v4_i16(); \
		explicit operator v4_i32(); \
		explicit operator v4_i64(); \
		explicit operator v4_f32(); \
		explicit operator v4_f64(); \
	};
VEC_TYPES
#undef VEC_TYPE

// define cast operators
#define VEC_TYPE(type) \
	inline v2_##type::operator v2_u8()  { return v2_u8 ((u8)x,  (u8)y);  }; \
	inline v2_##type::operator v2_u16() { return v2_u16((u16)x, (u16)y); }; \
	inline v2_##type::operator v2_u32() { return v2_u32((u32)x, (u32)y); }; \
	inline v2_##type::operator v2_u64() { return v2_u64((u64)x, (u64)y); }; \
	inline v2_##type::operator v2_i8()  { return v2_i8 ((i8)x,  (i8)y);  }; \
	inline v2_##type::operator v2_i16() { return v2_i16((i16)x, (i16)y); }; \
	inline v2_##type::operator v2_i32() { return v2_i32((i32)x, (i32)y); }; \
	inline v2_##type::operator v2_i64() { return v2_i64((i64)x, (i64)y); }; \
	inline v2_##type::operator v2_f32() { return v2_f32((f32)x, (f32)y); }; \
	inline v2_##type::operator v2_f64() { return v2_f64((f64)x, (f64)y); }; \
	inline v3_##type::operator v3_u8()  { return v3_u8 ((u8)x,  (u8)y,  (u8)z ); }; \
	inline v3_##type::operator v3_u16() { return v3_u16((u16)x, (u16)y, (u16)z); }; \
	inline v3_##type::operator v3_u32() { return v3_u32((u32)x, (u32)y, (u32)z); }; \
	inline v3_##type::operator v3_u64() { return v3_u64((u64)x, (u64)y, (u64)z); }; \
	inline v3_##type::operator v3_i8()  { return v3_i8 ((i8)x,  (i8)y,  (i8)z);  }; \
	inline v3_##type::operator v3_i16() { return v3_i16((i16)x, (i16)y, (i16)z); }; \
	inline v3_##type::operator v3_i32() { return v3_i32((i32)x, (i32)y, (i32)z); }; \
	inline v3_##type::operator v3_i64() { return v3_i64((i64)x, (i64)y, (i64)z); }; \
	inline v3_##type::operator v3_f32() { return v3_f32((f32)x, (f32)y, (f32)z); }; \
	inline v3_##type::operator v3_f64() { return v3_f64((f64)x, (f64)y, (f64)z); }; \
	inline v4_##type::operator v4_u8()  { return v4_u8 ((u8)x,  (u8)y,  (u8)z,  (u8)w ); }; \
	inline v4_##type::operator v4_u16() { return v4_u16((u16)x, (u16)y, (u16)z, (u16)w); }; \
	inline v4_##type::operator v4_u32() { return v4_u32((u32)x, (u32)y, (u32)z, (u32)w); }; \
	inline v4_##type::operator v4_u64() { return v4_u64((u64)x, (u64)y, (u64)z, (u64)w); }; \
	inline v4_##type::operator v4_i8()  { return v4_i8 ((i8)x,  (i8)y,  (i8)z,  (i8)w);  }; \
	inline v4_##type::operator v4_i16() { return v4_i16((i16)x, (i16)y, (i16)z, (i16)w); }; \
	inline v4_##type::operator v4_i32() { return v4_i32((i32)x, (i32)y, (i32)z, (i32)w); }; \
	inline v4_##type::operator v4_i64() { return v4_i64((i64)x, (i64)y, (i64)z, (i64)w); }; \
	inline v4_##type::operator v4_f32() { return v4_f32((f32)x, (f32)y, (f32)z, (f32)w); }; \
	inline v4_##type::operator v4_f64() { return v4_f64((f64)x, (f64)y, (f64)z, (f64)w); };
VEC_TYPES
#undef VEC_TYPE

// define operator overloads
#define VEC_TYPE(type) \
	inline v2_##type operator+(v2_##type a, v2_##type b) \
	{ \
		return v2_##type(a.x + b.x, a.y + b.y); \
	} \
	inline v2_##type operator-(v2_##type a, v2_##type b) \
	{ \
		return v2_##type(a.x - b.x, a.y - b.y); \
	} \
	inline v2_##type operator*(v2_##type a, v2_##type b) \
	{ \
		return v2_##type(a.x * b.x, a.y * b.y); \
	} \
	inline v4_##type operator*(v4_##type a, v4_##type b) \
	{ \
		return v4_##type(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); \
	} \
	inline v2_##type operator/(v2_##type a, v2_##type b) \
	{ \
		return v2_##type(a.x / b.x, a.y / b.y); \
	} \
	inline v2_##type operator+(type s, v2_##type v) \
	{ \
		return v2_##type(s + v.x, s + v.y); \
	} \
	inline v2_##type operator-(type s, v2_##type v) \
	{ \
		return v2_##type(s - v.x, s - v.y); \
	} \
	inline v2_##type operator*(type s, v2_##type v) \
	{ \
		return v2_##type(s * v.x, s * v.y); \
	} \
	inline v4_##type operator*(type s, v4_##type v) \
	{ \
		return v4_##type(s * v.x, s * v.y, s * v.z, s * v.w); \
	} \
	inline v2_##type operator/(type s, v2_##type v) \
	{ \
		return v2_##type(s / v.x, s / v.y); \
	} \
	inline v2_##type operator+(v2_##type v, type s) \
	{ \
		return v2_##type(v.x + s, v.y + s); \
	} \
	inline v2_##type operator-(v2_##type v, type s) \
	{ \
		return v2_##type(v.x - s, v.y - s); \
	} \
	inline v2_##type operator*(v2_##type v, type s) \
	{ \
		return v2_##type(v.x * s, v.y * s); \
	} \
	inline v4_##type operator*(v4_##type v, type s) \
	{ \
		return v4_##type(v.x * s, v.y * s, v.z * s, v.w * s); \
	} \
	inline v2_##type operator/(v2_##type v, type s) \
	{ \
		return v2_##type(v.x / s, v.y / s); \
	} \
	inline bool operator==(v2_##type a, v2_##type b) \
	{ \
		return a.x == b.x && a.y == b.y; \
	} \
	inline bool operator!=(v2_##type a, v2_##type b) \
	{ \
		return a.x != b.x || a.y != b.y; \
	} \
	inline void operator+=(v2_##type& a, v2_##type b) \
	{ \
		a = a + b; \
	} \
	inline void operator+=(v2_##type& a, type b) \
	{ \
		a = a + b; \
	} \
	inline void operator-=(v2_##type& a, v2_##type b) \
	{ \
		a = a - b; \
	} \
	inline void operator-=(v2_##type& a, type b) \
	{ \
		a = a - b; \
	} \
	inline void operator*=(v2_##type& a, v2_##type b) \
	{ \
		a = a * b; \
	} \
	inline void operator*=(v2_##type& a, type b) \
	{ \
		a = a * b; \
	} \
	inline void operator*=(v4_##type& a, v4_##type b) \
	{ \
		a = a * b; \
	} \
	inline void operator*=(v4_##type& a, type b) \
	{ \
		a = a * b; \
	}
VEC_TYPES
#undef VEC_TYPE

#define VEC_TYPE(type) \
	inline v2_##type operator-(v2_##type& v) \
	{ \
		return v2_##type(-v.x, -v.y); \
	}
SIGNED_VEC_TYPES
#undef VEC_TYPE

#undef VEC_TYPES

typedef v2_f32 v2;
typedef v3_f32 v3;
typedef v4_f32 v4;
typedef v4_u8 Color;

#endif

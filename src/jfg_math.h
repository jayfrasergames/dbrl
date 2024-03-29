#ifndef JFG_MATH_H
#define JFG_MATH_H

#include "prelude.h"
#include "containers.hpp"
#include <math.h>

// a tad excessive...
#define PI_F64 3.141592653589793238462643383279502884197169399375105820974944
#define PI_F32 3.141592653589793238462643383279502884197169399375105820974944f

// TODO -- use function overloading
#define MAKE_MIN_MAX_FUNCTIONS(Type) \
	static inline Type min_##Type(Type a, Type b) \
	{ \
		return a < b ? a : b; \
	} \
	static inline Type max_##Type(Type a, Type b) \
	{ \
		return a > b ? a : b; \
	}

MAKE_MIN_MAX_FUNCTIONS(u32)
MAKE_MIN_MAX_FUNCTIONS(i32)
MAKE_MIN_MAX_FUNCTIONS(f32)

static inline v2_u32 max_v2_u32(v2_u32 a, v2_u32 b)
{
	return v2_u32(max_u32(a.x, b.x), max_u32(a.y, b.y));
}

static inline v2_u32 jfg_max(v2_u32 a, v2_u32 b)
{
	return v2_u32(max_u32(a.x, b.x), max_u32(a.y, b.y));
}

static inline v2_u32 jfg_min(v2_u32 a, v2_u32 b)
{
	return v2_u32(min_u32(a.x, b.x), min_u32(a.y, b.y));
}

static inline i32 jfg_max(i32 a, i32 b)
{
	return a > b ? a : b;
}

static inline i32 jfg_max(i32 a, i32 b, i32 c, i32 d)
{
	return jfg_max(jfg_max(jfg_max(a, b), c), d);
}

#undef MAKE_MIN_MAX_FUNCTIONS

static inline f32 lerp(f32 start, f32 end, f32 weight)
{
	return start + (end - start) * weight;
}

static inline v2 lerp(v2 start, v2 end, f32 weight)
{
	return start + (end - start) * weight;
}

static inline f32 clamp(f32 val, f32 low, f32 high)
{
	return min_f32(max_f32(low, val), high);
}

static inline f32 dot(v2 a, v2 b)
{
	return a.x * b.x + a.y * b.y;
}

struct m2
{
	union {
		struct {
			f32 _00, _10, _01, _11;
		};
		f32 elements[4];
		v2 rows[2];
	};

	static m2 rotation(f32 theta)
	{
		f32 c = cosf(theta), s = sinf(theta);
		m2 m;
		m._00 = c; m._10 = -s;
		m._01 = s; m._11 =  c;
		return m;
	}
};

static inline v2 operator*(m2 m, v2 v)
{
	return v2(m._00 * v.x + m._10 * v.y, m._01 * v.x + m._11 * v.y);
}

struct Rational
{
	i32 numerator;
	i32 denominator;

	static Rational cancel(i32 n, i32 d)
	{
		Rational result = {};
		if (d) {
			u32 a = abs(n), b = abs(d);
			while (u32 t = a % b) {
				a = b;
				b = t;
			}
			i32 gcd = (i32)b;
			ASSERT(n % gcd == 0 && d % gcd == 0);
			result.numerator = n / gcd;
			result.denominator = d / gcd;
		} else {
			result.numerator = n / abs(n);
			result.denominator = 0;
		}
		return result;
	}
};

static inline bool operator>(Rational a, Rational b)
{
	i32 denominator = a.denominator * b.denominator;
	bool result = a.numerator * b.denominator > b.numerator * a.denominator;
	if (denominator < 0) {
		result = !result;
	}
	return result;
}

static inline bool operator>=(Rational a, Rational b)
{
	i32 denominator = a.denominator * b.denominator;
	bool result = a.numerator * b.denominator >= b.numerator * a.denominator;
	if (denominator < 0) {
		result = !result;
	}
	return result;
}

static inline bool operator<(Rational a, Rational b)
{
	i32 denominator = a.denominator * b.denominator;
	bool result = a.numerator * b.denominator < b.numerator * a.denominator;
	if (denominator < 0) {
		result = !result;
	}
	return result;
}

static inline bool operator<=(Rational a, Rational b)
{
	i32 denominator = a.denominator * b.denominator;
	bool result = a.numerator * b.denominator <= b.numerator * a.denominator;
	if (denominator < 0) {
		result = !result;
	}
	return result;
}

void calc_line(v2_i32 start, v2_i32 end, Output_Buffer<v2_i32> line);

#endif

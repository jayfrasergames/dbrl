#ifndef CPU_GPU_DATA_TYPES_H
#define CPU_GPU_DATA_TYPES_H

#ifdef JFG_HLSL

	#define F1 float
	#define F2 float2
	#define F3 float3
	#define F4 float4

	#define U1 uint
	#define U2 uint2
	#define U3 uint3
	#define U4 uint4

	#define I1 int
	#define I2 int2
	#define I3 int3
	#define I4 int4

#else

	#define F1 f32
	#define F2 v2
	#define F3 v3
	#define F4 v4

	#define U1 u32
	#define U2 v2_u32
	#define U3 v3_u32
	#define U4 v4_u32

	#define I1 i32
	#define I2 v2_i32
	#define I3 v3_i32
	#define I4 v4_i32

#endif

#endif

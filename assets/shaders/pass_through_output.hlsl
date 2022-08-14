Texture2D<float4> tex : register(t0);

static const float2 PASS_THROUGH_VERTICES[] = {
	float2(-1.0f, -1.0f),
	float2(-1.0f,  1.0f),
	float2( 1.0f, -1.0f),

	float2( 1.0f, -1.0f),
	float2(-1.0f,  1.0f),
	float2( 1.0f,  1.0f),
};

float4 vs_pass_through(uint vid : SV_VertexID) : SV_Position
{
	float2 vertex = PASS_THROUGH_VERTICES[vid];
	return float4(vertex, 0.0f, 1.0f);
}

float4 ps_pass_through(float4 pixel_center : SV_Position) : SV_Target
{
	uint2 pixel_index = uint2(pixel_center.xy);
	return float4(tex[pixel_index].rgb, 1.0f);
}

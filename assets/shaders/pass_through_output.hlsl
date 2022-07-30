Texture2D<float4> tex : register(t0);

static const float2 PASS_THROUGH_VERTICES[] = {
	{ -1.0f, -1.0f },
	{ -1.0f,  1.0f },
	{  1.0f, -1.0f },

	{  1.0f, -1.0f },
	{ -1.0f,  1.0f },
	{  1.0f,  1.0f },
};

float4 vs_pass_through(uint vid : SV_VertexID) : SV_Position
{
	float2 vertex = PASS_THROUGH_VERTICES[vid];
	return float4(vertex, 0.0f, 1.0f);
}

float4 ps_pass_through(float4 pixel_center : SV_Position) : SV_Target
{
	uint2 pixel_index = uint2(pixel_center.xy);
	return tex[pixel_index];
}

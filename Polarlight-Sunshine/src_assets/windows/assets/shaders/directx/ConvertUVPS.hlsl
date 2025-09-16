Texture2D image : register(t0);
Texture2D watermark : register(t1);

SamplerState def_sampler : register(s0);

struct FragTexWide {
	float3 uuv : TEXCOORD0;
};

cbuffer ColorMatrix : register(b0) {
	float4 color_vec_y;
	float4 color_vec_u;
	float4 color_vec_v;
	float2 range_y;
	float2 range_uv;
};

cbuffer TileParams : register(b1) {
    float tileCount; // 平铺次数（例如 4.0 表示重复4x4次）
};

// 在调用前定义 GetLuminance 函数
float GetLuminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722)); // BT.709 亮度公式
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float2 main_ps(FragTexWide input) : SV_Target
{
	// 采样源图片和水印
    float4 sourceColor_left = image.Sample(def_sampler, input.uuv.xz);
    float4 watermarkColor_left = watermark.Sample(def_sampler, input.uuv.xz * tileCount);

	float4 sourceColor_right = image.Sample(def_sampler, input.uuv.yz);
    float4 watermarkColor_right = watermark.Sample(def_sampler, input.uuv.yz * tileCount);
// 计算源颜色亮度
    float sourceLuma_left = GetLuminance(sourceColor_left.rgb);
	float sourceLuma_right = GetLuminance(sourceColor_right.rgb);
	float sourceLuma = sourceLuma_left * 0.5 + sourceLuma_right * 0.5;

    // 定义亮度阈值（可调整为 0.3~0.5）
    const float brightnessThreshold = 0.3;
	float3 rgb_left = sourceColor_left.rgb;
	float3 rgb_right = sourceColor_right.rgb;
	if (watermarkColor_left.a != 0.0f || watermarkColor_right.a != 0.0f) {
    	if (sourceLuma > brightnessThreshold) {
        	rgb_left = (sourceColor_left - watermarkColor_left).rgb;
       	 	rgb_right = (sourceColor_right - watermarkColor_right).rgb;
		} else {
        	rgb_left = (watermarkColor_left + sourceColor_left).rgb;
        	rgb_right = (watermarkColor_right + sourceColor_right).rgb;
		}
	}

	float3 rgb = (rgb_left + rgb_right) * 0.5;

	float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
	float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;

	u = u * range_uv.x + range_uv.y;
	v = v * range_uv.x + range_uv.y;

	return float2(u, v);
}
Texture2D image : register(t0);
Texture2D watermark : register(t1);

SamplerState def_sampler : register(s0);

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

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD;
};

// 在调用前定义 GetLuminance 函数
float GetLuminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722)); // BT.709 亮度公式
}

float main_ps(PS_INPUT frag_in) : SV_Target
{
	// 采样源图片和水印
    float4 sourceColor = image.Sample(def_sampler, frag_in.tex);
    float4 watermarkColor = watermark.Sample(def_sampler, frag_in.tex * tileCount);
    // 计算源颜色亮度
    float sourceLuma = GetLuminance(sourceColor.rgb);

    // 定义亮度阈值（可调整为 0.3~0.5）
    const float brightnessThreshold = 0.3;
	float3 rgb = sourceColor.rgb;
	if (watermarkColor.a != 0.0f) {
    	if (sourceLuma > brightnessThreshold) {
        	rgb = (sourceColor - watermarkColor).rgb;
		} else {
        	rgb = (watermarkColor + sourceColor).rgb;
		}
	}
	// 采样原始画面和水印
    //float3 rgb = watermark.Sample(def_sampler, frag_in.tex, 0).rgb;
	float y = dot(color_vec_y.xyz, rgb) + color_vec_y.w;

	return y * range_y.x + range_y.y;
}
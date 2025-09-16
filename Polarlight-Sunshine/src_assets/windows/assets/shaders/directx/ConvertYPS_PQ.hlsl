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

float3 NitsToPQ(float3 L)
{
    // Constants from SMPTE 2084 PQ
    static const float m1 = 2610.0 / 4096.0 / 4;
    static const float m2 = 2523.0 / 4096.0 * 128;
    static const float c1 = 3424.0 / 4096.0;
    static const float c2 = 2413.0 / 4096.0 * 32;
    static const float c3 = 2392.0 / 4096.0 * 32;

    float3 Lp = pow(saturate(L / 10000.0), m1);
    return pow((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float3 Rec709toRec2020(float3 rec709)
{
    static const float3x3 ConvMat =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(ConvMat, rec709);
}

float3 scRGBTo2100PQ(float3 rgb)
{
    // Convert from Rec 709 primaries (used by scRGB) to Rec 2020 primaries (used by Rec 2100)
    rgb = Rec709toRec2020(rgb);

    // 1.0f is defined as 80 nits in the scRGB colorspace
    rgb *= 80;

    // Apply the PQ transfer function on the raw color values in nits
    return NitsToPQ(rgb);
}

// 在调用前定义 GetLuminance 函数
float GetLuminance(float3 color) {
    return dot(color, float3(0.2627, 0.6780, 0.0593)); // BT.2020亮度计算
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

    float3 frgb = scRGBTo2100PQ(rgb);

    float y = dot(color_vec_y.xyz, frgb) + color_vec_y.w;

    return y * range_y.x + range_y.y;
}

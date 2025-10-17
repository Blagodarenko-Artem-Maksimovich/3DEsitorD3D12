struct PSIn
{
    float4 PosH : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

float4 main(PSIn i) : SV_TARGET
{
    float3 lightDir = normalize(float3(-0.6f, -1.0f, -0.3f));
    float nDotL = saturate(dot(i.Normal, -lightDir));
    float3 baseLow = float3(0.08, 0.45, 0.08);
    float3 baseHigh = float3(0.6, 0.5, 0.35);
    float3 baseColor = lerp(baseLow, baseHigh, i.UV.y);
    float3 color = baseColor * (0.2 + 0.8 * nDotL);
    return float4(color, 1.0f);
}

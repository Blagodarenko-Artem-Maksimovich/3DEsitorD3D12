// Default.hlsl - УПРОЩЕННАЯ ВЕРСИЯ
struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
};

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

cbuffer cbPass : register(b1)
{
    float4x4 gViewProj;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(posW, gViewProj);
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f); // Просто красный цвет
}
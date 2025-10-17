cbuffer PerObject : register(b0)
{
    matrix gWorldViewProj;
};

struct VSIn
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct PSIn
{
    float4 PosH : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

PSIn main(VSIn input)
{
    PSIn o;
    o.PosH = mul(float4(input.Pos, 1.0f), gWorldViewProj);
    o.Normal = normalize(input.Normal);
    o.UV = input.UV;
    return o;
}

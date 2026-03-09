// ShadowMap.hlsl

Texture2D gDiffuseMap : register(t0);
SamplerState gsamLinearWrap : register(s0);

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

// Same ObjectConstants as in your main shaders for gWorld
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld; // Not used in shadow pass typically
    float4x4 gTexTransform;
};

// Pass constants for the shadow pass (Light's View-Projection matrix)
cbuffer cbPassShadow : register(b1) // Using b1, ensure it's distinct or managed
{
    float4x4 gLightViewProj;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

    // Transform to light's clip space.
    vout.PosH = mul(posW, gLightViewProj);

    // Pass through texcoords (for alpha test)
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = texC.xy;
    
    return vout;
}

// Alpha-tested shadow caster: clip by texture alpha.
// For fully opaque textures alpha=1, so this is a no-op.
void PS(VertexOut pin)
{
    float a = gDiffuseMap.Sample(gsamLinearWrap, pin.TexC).a;
    clip(a - 0.5f);
}

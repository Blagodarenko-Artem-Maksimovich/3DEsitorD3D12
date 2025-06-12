// ShadowMap.hlsl

struct VertexIn
{
    float3 PosL : POSITION;
    // Other attributes like TexC might be needed if using alpha testing for shadows
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    // Potentially TexC if doing alpha testing
};

// Same ObjectConstants as in your main shaders for gWorld
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld; // Not used in shadow pass typically
    float4x4 gTexTransform; // Not used in shadow pass typically
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
    
    return vout;
}

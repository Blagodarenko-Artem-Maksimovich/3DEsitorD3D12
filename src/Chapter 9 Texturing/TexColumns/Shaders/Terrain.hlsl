// Terrain: vertex displacement from heightmap, same GBuffer output as GeometryPass
#include "LightingUtil.hlsl"
Texture2D gHeightMap : register(t0);
Texture2D gDiffuseMap : register(t1);

SamplerState gsamLinearClamp : register(s3);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    float4x4 gTexTransform;
    float4x4 gPrevWorld;
};

// Must match PassConstants layout exactly (View, InvView, Proj, InvProj, ViewProj, InvViewProj, EyePosW, ...)
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float4x4 gViewProjNoJitter;
    float4x4 gPrevViewProjNoJitter;
    float4x4 gPrevViewProj;
    float2 gCurrJitterUV;
    float2 gPrevJitterUV;
    float2 g_padTaa;
    Light gLights[MaxLights];
};

// gHeightScale = Y scale from gWorld (World matrix has scale (sx, heightScale, sz))
struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 Tan : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float3 Tan : TANGENT;
    float4 CurrClip : TEXCOORD1;
    float4 PrevClip : TEXCOORD2;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    float gHeightScale = gWorld._22; // Y scale from world matrix
    float h = gHeightMap.SampleLevel(gsamLinearClamp, vin.TexC, 0).r;
    float3 posL = float3(vin.PosL.x, h * gHeightScale, vin.PosL.z);
    float4 posW = mul(float4(posL, 1.f), gWorld);
    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gViewProj);

    float2 uv = vin.TexC;
    float2 du = float2(1.f / 512.f, 0.f);
    float2 dv = float2(0.f, 1.f / 512.f);
    float hL = gHeightMap.SampleLevel(gsamLinearClamp, uv - du, 0).r;
    float hR = gHeightMap.SampleLevel(gsamLinearClamp, uv + du, 0).r;
    float hD = gHeightMap.SampleLevel(gsamLinearClamp, uv - dv, 0).r;
    float hU = gHeightMap.SampleLevel(gsamLinearClamp, uv + dv, 0).r;
    float3 nL = normalize(float3(-(hR - hL) * gHeightScale, 2.f, -(hU - hD) * gHeightScale));
    vout.NormalW = mul(nL, (float3x3)gWorld);
    vout.Tan = mul(float3(1, 0, 0), (float3x3)gWorld);

    vout.TexC = mul(float4(vin.TexC, 0, 1), gTexTransform).xy;
    vout.CurrClip = mul(posW, gViewProjNoJitter);
    float4 prevW = mul(float4(posL, 1.f), gPrevWorld);
    vout.PrevClip = mul(prevW, gPrevViewProjNoJitter);
    return vout;
}

// Same PS output as GeometryPass for GBuffer
struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Position : SV_Target2;
    float2 Velocity : SV_Target3;
};

// Material in same b2 as main geometry pass
cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float gMetallic;
    float3 _padM;
    float4x4 gMatTransform;
};

PSOutput PS(VertexOut pin)
{
    PSOutput outt;
    float4 diffuseTex = gDiffuseMap.Sample(gsamLinearClamp, pin.TexC);
    outt.Albedo = diffuseTex * gDiffuseAlbedo;
    outt.Albedo.a = gRoughness;
    outt.Normal = float4(normalize(pin.NormalW), gMetallic);
    outt.Position = float4(pin.PosW, 1.f);
    float invWc = (abs(pin.CurrClip.w) > 1e-6f) ? (1.f / pin.CurrClip.w) : 0.f;
    float invWp = (abs(pin.PrevClip.w) > 1e-6f) ? (1.f / pin.PrevClip.w) : 0.f;
    float2 currNdc = pin.CurrClip.xy * invWc;
    float2 prevNdc = pin.PrevClip.xy * invWp;
    outt.Velocity = (prevNdc - currNdc) * float2(0.5f, -0.5f);
    return outt;
}

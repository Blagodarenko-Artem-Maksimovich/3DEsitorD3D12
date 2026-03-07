
#include "LightingUtil.hlsl"
Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);


SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    float4x4 gTexTransform;
    float4x4 gPrevWorld;
};

// Constant data that varies per material.
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
    float4x4 gViewProjNoJitter; // NEW
    float4x4 gPrevViewProjNoJitter; // NEW
    float4x4 gPrevViewProj;
    float2 gCurrJitterUV;
    float2 gPrevJitterUV;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float gMetallic;
    float3 _padMetallic;
    float4x4 gMatTransform;
};

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

    float4 CurrClip : TEXCOORD1; // NDC current (no jitter)
    float4 PrevClip : TEXCOORD2; // NDC previous (no jitter)
};


VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // rasterization uses jittered VP
    vout.PosH = mul(posW, gViewProj);

    // velocity uses no-jitter VP
    vout.CurrClip = mul(posW, gViewProjNoJitter);

    float4 prevW = mul(float4(vin.PosL, 1.0f), gPrevWorld);
    vout.PrevClip = mul(prevW, gPrevViewProjNoJitter);

    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    vout.Tan = mul(vin.Tan, (float3x3) gWorld);

    return vout;
}



// PSOutput � ����������� ��������
struct PSOutput
{
    float4 Albedo : SV_Target0; // Diffuse color
    float4 Normal : SV_Target1; // Normal.xyz, alpha ����� ������ =1
    float4 Position : SV_Target2; // Position.xyz, alpha=1
    float2 Velocity : SV_Target3; // ������ R16G16_FLOAT
};

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
	// Uncompress each component from [0,1] to [-1,1].
    float3 normalT = 2.0f * normalMapSample - 1.0f;

	// Build orthonormal basis.
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

	// Transform from tangent space to world space.
    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}

PSOutput PS(VertexOut pin)
{
    PSOutput outt;

    float4 diffuseTex = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC);
    outt.Albedo = diffuseTex * gDiffuseAlbedo;
    // Pack roughness into Albedo.a for deferred PBR.
    outt.Albedo.a = gRoughness;

    float3 normalSample = gNormalMap.Sample(gsamAnisotropicWrap, pin.TexC).xyz;
    pin.NormalW = normalize(pin.NormalW);
    float3 normalW = NormalSampleToWorldSpace(normalSample.rgb, pin.NormalW, pin.Tan);
    // Pack metallic into Normal.a for deferred PBR.
    outt.Normal = float4(normalW, gMetallic);

    outt.Position = float4(pin.PosW, 1.0f);

    // Velocity (UV offset)
    float invWc = (abs(pin.CurrClip.w) > 1e-6f) ? (1.0f / pin.CurrClip.w) : 0.0f;
    float invWp = (abs(pin.PrevClip.w) > 1e-6f) ? (1.0f / pin.PrevClip.w) : 0.0f;

    float2 currNdc = pin.CurrClip.xy * invWc;
    float2 prevNdc = pin.PrevClip.xy * invWp;

// NDC velocity
    float2 velNdc = prevNdc - currNdc;

// NDC -> UV offset
    float2 velUV = velNdc * float2(0.5f, -0.5f);
    outt.Velocity = velUV;


    return outt;
}

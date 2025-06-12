// GBuffer.hlsl

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
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW;

    vout.PosH = mul(posW, gViewProj);
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;
    
    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    vout.Tan = mul(vin.Tan, (float3x3) gWorld);
    // Transform to homogeneous clip space.

	// Output vertex attributes for interpolation across triangle.
 
    
    return vout;
}

// PSOutput с несколькими буферами
struct PSOutput
{
    float4 Albedo : SV_Target0; // Diffuse color
    float4 Normal : SV_Target1; // Normal.xyz, alpha можно задать =1
    float4 Position : SV_Target2; // Position.xyz, alpha=1
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
    // Сэмплим диффузную текстуру
    float4 diffuseTex = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC);
    outt.Albedo = diffuseTex * gDiffuseAlbedo; // альбедо (RGB), альфа можем взять из diffuseAlbedo.a

    // Обрабатываем нормаль: из карты или вершинная
    float3 normalW;
    // Сэмплируем карту нормалей в неликвидном пространстве (0..1 -> -1..1)
    float3 normalSample = gNormalMap.Sample(gsamAnisotropicWrap, pin.TexC).xyz;
    pin.NormalW = normalize(pin.NormalW);
    normalW = NormalSampleToWorldSpace(normalSample.rgb, pin.NormalW, pin.Tan);;

    outt.Normal = float4(normalW, 1.0f);

    // Позиция в мировых координатах
    outt.Position = float4(pin.PosW, 1.0f);

    
    
    return outt;
}
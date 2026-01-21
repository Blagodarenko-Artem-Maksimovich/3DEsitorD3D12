
struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction; // directional/spot light only
    float FalloffEnd; // point/spot light only
    float3 Position; // point light only
    float SpotPower; // spot light only
};

struct MaterialData
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
    uint DiffuseMapIndex;
    uint NormalMapIndex;
    uint MatPad1;
    uint MatPad2;
};
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);
Texture2D gTextureMaps[100] : register(t0);
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gPrevWorld;
    float4x4 gTexTransform;
    uint gMaterialIndex;
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gShadowTransform;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float4x4 gJitteredViewProj;
    float4x4 prevViewProj;
    Light gLights[16];
};

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);



struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float3 PosW : POSITION1;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
    float4 PrevPosH : POSITION2;
    float4 CurPosH : POSITION3;
};

// Выходная структура Пиксельного Шейдера
struct PixelOut
{
    float4 AlbedoRoughness : SV_Target0; // RT0
    float4 NormalFresnel : SV_Target1; // RT1
    float4 Position : SV_Target2; // RT2
    float2 Velocity : SV_Target3;
};

//---------------------------------------------------------------------------------------
// Transforms a normal map sample to world space.
//---------------------------------------------------------------------------------------
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


VertexOut VS(VertexIn vin)
{
    
    VertexOut vout = (VertexOut) 0.0f;

	// Fetch the material data.
    MaterialData matData = gMaterialData[gMaterialIndex];
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    float4 prevPosW = mul(float4(vin.PosL, 1.0f), gPrevWorld);
    
    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
	
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gJitteredViewProj);
    vout.PrevPosH = mul(prevPosW, prevViewProj);
    vout.CurPosH = mul(posW, gViewProj);
	// Output vertex attributes for interpolation across triangle.
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;

    // Generate projective tex-coords to project shadow map onto scene.
    vout.ShadowPosH = mul(posW, gShadowTransform);
	
    return vout;
}


float2 CalcVelocity(float4 newPos, float4 oldPos)
{
    oldPos /= oldPos.w;
    oldPos.xy = (oldPos.xy + 1) / 2.0f;
    oldPos.y = 1 - oldPos.y;
    
    newPos /= newPos.w;
    newPos.xy = (newPos.xy + 1) / 2.0f;
    newPos.y = 1 - newPos.y;
    
    return (newPos - oldPos).xy;
}


PixelOut PS(VertexOut pin) : SV_Target
{
    PixelOut pout;
    
	// Fetch the material data.
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
	
    // 1. Sample Albedo
    float4 texColor = gTextureMaps[NonUniformResourceIndex(diffuseMapIndex)].Sample(gsamAnisotropicWrap, pin.TexC);
    diffuseAlbedo *= texColor;
    
#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif

	// 2. Normal Mapping
    pin.NormalW = normalize(pin.NormalW);
    float4 normalMapSample = gTextureMaps[NonUniformResourceIndex(normalMapIndex)].Sample(gsamAnisotropicWrap, pin.TexC);
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, pin.NormalW, pin.TangentW);
    // RT0: Цвет (RGB) + Шероховатость (A)
    pout.AlbedoRoughness = float4(diffuseAlbedo.rgb, roughness);

    // RT1: Нормаль (RGB) + Френель (A) 
    pout.NormalFresnel = float4(bumpedNormalW, fresnelR0.r);

    // RT2: Мировая позиция (RGB) + Unused
    pout.Position = float4(pin.PosW, 1.0f);

    
    pout.Velocity = CalcVelocity(pin.CurPosH, pin.PrevPosH);
    
    return pout;
}



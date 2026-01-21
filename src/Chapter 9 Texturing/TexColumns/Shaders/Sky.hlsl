//=============================================================================
// Sky.fx by Frank Luna (C) 2011 All Rights Reserved.
//=============================================================================

// Include common HLSL code.

TextureCube gCubeMap : register(t0);
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);
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
};
struct VertexIn
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
    float4 CurPosH : POSITION2;
	float4 PrevPosH : POSITION1;
    float3 PosL : POSITION;
};
 
struct PSOut
{
    float4 BackBuffer : SV_Target0;
    float4 CurrentFrame : SV_Target1;
    float2 Velocity : SV_Target2;
};

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

VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	// Use local vertex position as cubemap lookup vector.
	vout.PosL = vin.PosL;
	
	// Transform to world space.
	float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

	// Always center sky about camera.
	posW.xyz += gEyePosW;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
	vout.PosH = mul(posW, gViewProj).xyww;
    vout.CurPosH = mul(posW, gViewProj).xyww;
    vout.PrevPosH = mul(posW, prevViewProj).xyww;
	return vout;
}

PSOut PS(VertexOut pin) : SV_Target
{
    PSOut pout;
    pout.BackBuffer = gCubeMap.Sample(gsamLinearWrap, pin.PosL);
    pout.CurrentFrame = gCubeMap.Sample(gsamLinearWrap, pin.PosL);
    pout.Velocity = CalcVelocity(pin.CurPosH, pin.PrevPosH);
    return pout;
}


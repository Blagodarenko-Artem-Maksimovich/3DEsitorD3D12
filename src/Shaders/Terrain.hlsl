struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

#define MaxLights 16

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    uint gMaterialIndex;
    uint gLODLevel;
    uint gObjPad1;
    uint gObjPad2;
}

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerPassPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float4 gFrustumPlanes[6];
    Light gLights[MaxLights];
}

cbuffer cbTerrain : register(b2)
{
    float gMinHeight;
    float gMaxHeight;
    float gTerrainSize;
    float gTexelSize;
    float2 gHeightMapSize;
    float2 gTerrainPadding;
}

Texture2D gHeightMap : register(t0);
Texture2D gDiffuseMap : register(t1);
Texture2D gNormalMap : register(t2);
Texture2D gPaintMap : register(t3);

SamplerState gsamLinearWrap : register(s0);
SamplerState gsamLinearClamp : register(s1);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

float ComputeHeightRange()
{
    return gMaxHeight - gMinHeight;
}

float2 GetHeightmapTexelSize()
{
    return 1.0f / gHeightMapSize;
}

float SampleHeight01(float2 uv)
{
    return gHeightMap.SampleLevel(gsamLinearClamp, saturate(uv), 0).r;
}

float SampleHeightWorld(float2 uv)
{
    float h01 = saturate(SampleHeight01(uv));
    return gMinHeight + h01 * ComputeHeightRange();
}

float3 ComputeNormalFromHeightmap(float2 uv)
{
    float2 texelSize = GetHeightmapTexelSize();
    
    float hL = SampleHeightWorld(uv + float2(-texelSize.x, 0.0f));
    float hR = SampleHeightWorld(uv + float2(texelSize.x, 0.0f));
    float hD = SampleHeightWorld(uv + float2(0.0f, -texelSize.y));
    float hU = SampleHeightWorld(uv + float2(0.0f, texelSize.y));
    
    float verticalScale = 2.0f * gTerrainSize / gHeightMapSize.x;
    
    float3 normal;
    normal.x = hL - hR;
    normal.y = verticalScale;
    normal.z = hD - hU;
    
    return normalize(normal);
}

float2 ApplyTextureTransform(float2 localUV)
{
    float4 transformed = mul(float4(localUV, 0.0f, 1.0f), gTexTransform);
    return transformed.xy;
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    float2 globalUV = ApplyTextureTransform(vin.TexC);
    
    float3 localPos = vin.PosL;
    localPos.y = 0.0f;
    
    float4 worldPos = mul(float4(localPos, 1.0f), gWorld);
    worldPos.y = SampleHeightWorld(globalUV);
    
    vout.PosW = worldPos.xyz;
    vout.NormalW = ComputeNormalFromHeightmap(globalUV);
    vout.PosH = mul(worldPos, gViewProj);
    vout.TexC = globalUV;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 normal = normalize(pin.NormalW);
    float3 baseColor = gDiffuseMap.Sample(gsamLinearClamp, pin.TexC).rgb;
    float4 paintColor = gPaintMap.Sample(gsamLinearClamp, pin.TexC);
    
    float3 albedo = lerp(baseColor, paintColor.rgb, paintColor.a);
    
    float3 lightDir = normalize(-gLights[0].Direction);
    float ndotl = max(dot(normal, lightDir), 0.0f);
    
    float3 ambient = gAmbientLight.rgb * 0.4f;
    float3 diffuse = gLights[0].Strength * ndotl;
    
    return float4((ambient + diffuse) * albedo, 1.0f);
}

float4 PS_Wireframe(VertexOut pin) : SV_Target
{
    uint lodIndex = gLODLevel % 5;
    
    float3 colors[5] =
    {
        float3(1.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 0.0f, 1.0f),
        float3(1.0f, 1.0f, 0.0f),
        float3(1.0f, 0.0f, 1.0f)
    };
    
    return float4(colors[lodIndex], 1.0f);
}
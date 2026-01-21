#include "LightingUtil.hlsl"

cbuffer cbPass : register(b0)
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
};

cbuffer LightConstants : register(b1)
{
    float3 Color;
    float FalloffStart; // point/spot light only
    float3 Direction; // directional/spot light only
    float FalloffEnd; // point/spot light only
    float3 Position; // point light only
    float SpotPower; // spot light only
    int type;
    float Strength;
    int CastsShadows;
    int isDebugOn;
    float4x4 gWorld;
    float4x4 LightViewProj; // World space to Light's clip space for shadow mapping
    int enablePCF;
    int pcf_level;
}
TextureCube gCubeMap : register(t0);
Texture2D gShadowMap : register(t1);

// Входные текстуры G-Buffer (Привяжем их в слоты t3, t4, t5 в новом Root Signature)
Texture2D gGBuffer0 : register(t2); // Albedo + Roughness
Texture2D gGBuffer1 : register(t3); // Normal + Fresnel
Texture2D gGBuffer2 : register(t4); // Position


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

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

float CalcShadowFactor(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);

    // Texel size.
    float dx = 1.0f / (float) width;

    float percentLit = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow,
            shadowPosH.xy + offsets[i], depth).r;
    }
    
    return percentLit / 9.0f;
}


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
    float2 TexC : TEXCOORD;
};


struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

struct PixelOut
{
    float4 BackBuffer : SV_Target0;
    float4 CurrentFrameTexture : SV_Target1;
};
// draw full-screen quad without VertexBuffer
VSOut VS_QUAD(uint vid : SV_VertexID)
{
    VSOut output;
    
    // Координаты вершин полноэкранного треугольника
    float2 positions[3] =
    {
        float2(-1, -1),
        float2(-1, 3),
        float2(3, -1)
    };
    
    output.PosH = float4(positions[vid], 0, 1);
    output.TexC = output.PosH.xy * 0.5 + 0.5;
    
    return output;
}

VSOut VS(VertexIn vin)
{
    VSOut vout = (VSOut) 0.0f;
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

    vout.PosH = mul(posW, gViewProj);
    
    return vout;
}


// Пиксельный шейдер освещения
PixelOut PS(VSOut pin) : SV_Target
{
    LightData light;
    light.Color = Color;
    light.FalloffStart = FalloffStart;
    light.Direction = Direction;
    light.FalloffEnd = FalloffEnd;
    light.Position = Position;
    light.SpotPower = SpotPower;
    light.type = type;
    light.Strength = Strength;
    light.CastsShadows = CastsShadows;
    light.isDebugOn = isDebugOn;
    light.gWorld = gWorld;
    light.LightViewProj = LightViewProj;
    light.enablePCF = enablePCF;
    light.pcf_level = pcf_level;
    
    float2 texelSize = 1.0f / float2(2048, 2048); // Pass these as constants
    
    int2 pix = int2(pin.PosH.xy);
    // Вычитываем G-Buffer
    float4 albedo = gGBuffer0.Load(int3(pix, 0));
    float3 normalW = normalize(gGBuffer1.Load(int3(pix, 0)).xyz);
    float3 posW = gGBuffer2.Load(int3(pix, 0)).xyz;
    float roughness = gGBuffer0.Load(int3(pix, 0)).a; 
    float3 fresnelR0 = gGBuffer1.Load(int3(pix, 0)).www;  
    float3 toEyeW = normalize(gEyePosW - posW);

    // Light terms.
    float4 ambient = gAmbientLight * albedo;

    Material mat =
    {
        albedo, fresnelR0, 1-roughness
    };
    float shadowFactor = 1.0f;
    
    float4 shadowPosH = mul(float4(posW, 1.0f), LightViewProj);
    
    shadowPosH.xyz /= shadowPosH.w;
    
    float2 shadowTexC;
    shadowTexC.x = 0.5f * shadowPosH.x + 0.5f;
    shadowTexC.y = -0.5f * shadowPosH.y + 0.5f; // If Y is inverted, otherwise +0.5f

    const float shadowBias = 0.001f; // Adjust this value to prevent shadow acne

    if ((saturate(shadowTexC.x) == shadowTexC.x) && (saturate(shadowTexC.y) == shadowTexC.y) && (shadowPosH.z > 0.0f) && (shadowPosH.z < 1.0f))
    {
        shadowFactor = gShadowMap.SampleCmpLevelZero(gsamShadow, shadowTexC, shadowPosH.z - shadowBias);

    // For simple Percentage Closer Filtering (PCF 2x2):
        if (enablePCF)
        {
            
            float totalFactor = 0.0f;
            for (float y = -pcf_level; y <= pcf_level; y += 1.0f)
            {
                for (float x = -pcf_level; x <= pcf_level; x += 1.0f)
                {
                    float2 offset = float2(x, y) * texelSize;
                    totalFactor += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowTexC + offset, shadowPosH.z - shadowBias);
                }
            }
            shadowFactor = totalFactor / ((pcf_level * 2 + 1) * (pcf_level * 2 + 1));
        }
        
    
    }
    else // Out of shadow map bounds, assume lit or handle as needed
    {
        shadowFactor = 1.0f;
    }
    if (!CastsShadows)
        shadowFactor = 1.0f;
    
    float3 lighting;
    switch (type)
    {
        case 0:
            lighting = Strength * Color * albedo.rgb;
            float3 r = reflect(-toEyeW, normalW);
            float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);
            float3 fresnelFactor = SchlickFresnel(fresnelR0, normalW, r);
            lighting += (1 - roughness) * fresnelFactor * reflectionColor.rgb * Strength * 2;
            
            break;
        case 1:
            lighting = ComputePointLight(light, mat, posW, normalW, toEyeW);
            break;
        case 2:
            lighting = ComputeDirectionalLight(light, mat, normalW, toEyeW, shadowFactor);
            break;
        case 3:
            lighting = ComputeSpotLight(light, mat, posW, normalW, toEyeW, shadowFactor);
            break;
    }
    
  
    float4 litColor = float4(lighting, 1);
    
    litColor.a = albedo.a;

    PixelOut pout;
    pout.BackBuffer = litColor;
    pout.CurrentFrameTexture = litColor;
    return pout;
}

PixelOut PS_debug(VSOut pin) 
{
    PixelOut pout;
    pout.BackBuffer = float4(1, 1, 1, 1);
    pout.CurrentFrameTexture = float4(1, 1, 1, 1);
    return pout;
}
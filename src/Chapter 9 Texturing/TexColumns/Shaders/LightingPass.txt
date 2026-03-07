// LightingPass.hlsl


#include "LightingUtil.hlsl"
Texture2D gShadowMap : register(t3); 
Texture2D gPositionMap : register(t2);
Texture2D gNormalMap : register(t1);
Texture2D gAlbedoMap : register(t0);
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6); 



cbuffer cbPass : register(b0)
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
cbuffer cbPerObject : register(b1)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    float4x4 gTexTransform;
};

cbuffer cbLight : register(b2)
{
    Light light;
}
// Вершинный шейдер для полноэкранного треугольника
struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

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
VSOut VS(VertexIn vin)
{
    VSOut vout = (VSOut) 0.0f;
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), light.gWorld);

    vout.PosH = mul(posW, gViewProj);
    
    return vout;
}

// Пиксельный шейдер освещения
float4 PS(VSOut pin) : SV_TARGET
{
    float2 texelSize = 1.0f / float2(2048, 2048); // Pass these as constants
    
    int2 pix = int2(pin.PosH.xy);
    // Вычитываем G-Buffer
    float4 albedo = gAlbedoMap.Load(int3(pix, 0));
    float3 normalW = normalize(gNormalMap.Load(int3(pix, 0)).xyz);
    float3 posW = gPositionMap.Load(int3(pix, 0)).xyz;

    float3 toEyeW = normalize(gEyePosW - posW);

    // Light terms.
    float4 ambient = gAmbientLight * albedo;

    Material mat =
    {
        albedo, float3(0.05, 0.05, 0.05), 0.7
    };
    float shadowFactor = 1.0f;
    
    float4 shadowPosH = mul(float4(posW, 1.0f), light.LightViewProj);
    
    shadowPosH.xyz /= shadowPosH.w;
    
    float2 shadowTexC;
    shadowTexC.x = 0.5f * shadowPosH.x + 0.5f;
    shadowTexC.y = -0.5f * shadowPosH.y + 0.5f; // If Y is inverted, otherwise +0.5f

    const float shadowBias = 0.001f; // Adjust this value to prevent shadow acne

    if ((saturate(shadowTexC.x) == shadowTexC.x) && (saturate(shadowTexC.y) == shadowTexC.y) && (shadowPosH.z > 0.0f) && (shadowPosH.z < 1.0f))
    {
        shadowFactor = gShadowMap.SampleCmpLevelZero(gsamShadow, shadowTexC, shadowPosH.z - shadowBias);

    // For simple Percentage Closer Filtering (PCF 2x2):
        if (light.enablePCF)
        {
            
            float totalFactor = 0.0f;
            for (float y = -light.pcf_level; y <= light.pcf_level; y += 1.0f)
            {
                for (float x = -light.pcf_level; x <= light.pcf_level; x += 1.0f)
                {
                    float2 offset = float2(x, y) * texelSize;
                    totalFactor += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowTexC + offset, shadowPosH.z - shadowBias);
                }
            }
            shadowFactor = totalFactor / ((light.pcf_level * 2 + 1) * (light.pcf_level * 2 + 1));
        }
        
    
    }
    else // Out of shadow map bounds, assume lit or handle as needed
    {
        shadowFactor = 1.0f;
    }
    if (!light.CastsShadows)
        shadowFactor = 1.0f;
    
    float3 lighting;
    switch (light.type)
    {
        case 0:
            lighting = light.Strength * albedo;
            break;
        case 1:
            lighting = ComputePointLight(light, mat, posW, normalW, toEyeW);
            break;
        case 2:
            lighting = ComputeDirectionalLight(light, mat, normalW, toEyeW,shadowFactor);
            break;
        case 3:
            lighting = ComputeSpotLight(light, mat, posW, normalW, toEyeW,shadowFactor);
           // lighting = float4(1, 1, 1, 1);
            break;
    }
    
  
    float4 litColor = float4(lighting, 1);
    // Common convention to take alpha from diffuse albedo.
    litColor.a = albedo.a;

    return litColor;
}
float4 PS_debug(VSOut pin) : SV_TARGET
{
    return float4(1, 1, 1, 1);
}
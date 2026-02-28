//=============================================================================
// Sky.hlsl - Sky rendering with atmospheric scattering
// Based on GPU Gems 2, Chapter 16: Accurate Atmospheric Scattering
//=============================================================================

#define PI 3.14159265359

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
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
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
};

// Atmosphere parameters
cbuffer cbAtmosphere : register(b2)
{
    float3 gSunDirection;
    float gSunIntensity;
    
    float3 gRayleighScattering;
    float gPlanetRadius;
    
    float3 gMieScattering;
    float gAtmosphereRadius;
    
    float gRayleighScaleHeight;
    float gMieScaleHeight;
    float gMieAnisotropy;
    float gAtmosphereDensity;
    
    float3 gCameraPositionKm;
    float gExposure;
    
    int gNumSamples;
    int gNumLightSamples;
    float2 gAtmoPad;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
};

// Rayleigh phase function
float RayleighPhase(float cosTheta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

// Henyey-Greenstein phase function for Mie scattering
float MiePhase(float cosTheta, float g)
{
    float g2 = g * g;
    float num = (1.0 - g2);
    float denom = 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / max(denom, 0.0001);
}

// Compute atmospheric scattering
float3 ComputeAtmosphericScattering(float3 rayDir)
{
    float3 sunDir = normalize(gSunDirection);
    float cosTheta = dot(rayDir, sunDir);
    
    // View angle from horizon
    float viewY = rayDir.y;
    
    // Optical depth - increases as we look toward horizon
    float zenithAngle = acos(max(viewY, 0.001));
    float opticalDepthRayleigh = exp(-0.0) / max(cos(zenithAngle), 0.035);
    opticalDepthRayleigh = min(opticalDepthRayleigh, 30.0);
    
    // Mie optical depth (affected by density/pollution)
    float opticalDepthMie = opticalDepthRayleigh * gAtmosphereDensity * 0.1;
    
    // Rayleigh scattering coefficients at sea level
    // These determine the blue color of the sky
    float3 betaR = float3(5.8e-3, 13.5e-3, 33.1e-3); // More scattering for blue
    
    // Mie scattering coefficient (haze/pollution)
    float3 betaM = float3(21e-3, 21e-3, 21e-3) * gAtmosphereDensity;
    
    // Phase functions
    float phaseR = RayleighPhase(cosTheta);
    float phaseM = MiePhase(cosTheta, gMieAnisotropy);
    
    // Extinction (how much light is absorbed/scattered out)
    float3 extinction = exp(-(betaR * opticalDepthRayleigh + betaM * opticalDepthMie));
    
    // Sun transmittance (color of sunlight after passing through atmosphere)
    float sunZenith = acos(max(sunDir.y, 0.001));
    float sunOpticalDepth = 1.0 / max(cos(sunZenith), 0.035);
    sunOpticalDepth = min(sunOpticalDepth, 30.0);
    float3 sunTransmittance = exp(-(betaR * sunOpticalDepth + betaM * sunOpticalDepth * 0.1));
    
    // In-scattering
    float3 rayleighScatter = betaR * phaseR * (1.0 - extinction);
    float3 mieScatter = betaM * phaseM * (1.0 - exp(-opticalDepthMie));
    
    // Combine scattering with sun color
    float3 sunColor = sunTransmittance * gSunIntensity;
    float3 inscatter = (rayleighScatter + mieScatter) * sunColor;
    
    // Add ambient sky light (indirect illumination)
    float3 ambientSky = float3(0.05, 0.1, 0.2) * (1.0 - extinction) * gSunIntensity * 0.1;
    
    float3 skyColor = inscatter + ambientSky;
    
    // Ground color when looking down
    if (viewY < 0.0)
    {
        float groundFade = saturate(-viewY * 3.0);
        float3 groundColor = float3(0.4, 0.35, 0.3) * sunTransmittance * gSunIntensity * 0.05;
        skyColor = lerp(skyColor, groundColor, groundFade);
    }
    
    return skyColor;
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    vout.PosL = vin.PosL;
    
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    posW.xyz += gEyePosW;
    
    // Set z = w so that z/w = 1 (skydome always on far plane)
    vout.PosH = mul(posW, gViewProj).xyww;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 rayDir = normalize(pin.PosL);
    
    // Compute atmospheric scattering
    float3 color = ComputeAtmosphericScattering(rayDir);
    
    // Add sun disk
    float3 sunDir = normalize(gSunDirection);
    float sunDot = dot(rayDir, sunDir);
    
    // Sun disk
    float sunDisk = smoothstep(0.9997, 0.9999, sunDot);
    float3 sunColor = float3(1.0, 0.95, 0.9) * gSunIntensity * 0.15 * sunDisk;
    
    // Sun corona/glow
    float sunGlow = pow(max(sunDot, 0.0), 512.0) * gSunIntensity * 0.3;
    sunGlow += pow(max(sunDot, 0.0), 64.0) * gSunIntensity * 0.05;
    float3 glowColor = float3(1.0, 0.9, 0.7) * sunGlow;
    
    color += sunColor + glowColor;
    
    // Tone mapping (Reinhard)
    color = color / (color + 1.0);
    
    // Exposure adjustment
    color = 1.0 - exp(-gExposure * color * 2.0);
    
    // Gamma correction
    color = pow(max(color, 0.0), 1.0 / 2.2);
    
    return float4(color, 1.0);
}

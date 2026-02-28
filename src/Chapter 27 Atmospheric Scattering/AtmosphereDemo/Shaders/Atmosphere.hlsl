//=============================================================================
// Atmosphere.hlsl - Atmospheric Scattering Shader
// Based on GPU Gems 2, Chapter 16: Accurate Atmospheric Scattering
// and Sean O'Neil's atmospheric scattering model
//=============================================================================

#define PI 3.14159265359

// Atmosphere parameters constant buffer
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
    float denom = pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return (1.0 / (4.0 * PI)) * num / denom;
}

// Calculate optical depth along a ray
// Returns (Rayleigh optical depth, Mie optical depth)
float2 ComputeOpticalDepth(float3 rayOrigin, float3 rayDir, float rayLength, int numSamples)
{
    float segmentLength = rayLength / float(numSamples);
    float2 opticalDepth = float2(0.0, 0.0);
    
    for (int i = 0; i < numSamples; ++i)
    {
        float3 samplePoint = rayOrigin + rayDir * (float(i) + 0.5) * segmentLength;
        float height = length(samplePoint) - gPlanetRadius;
        
        // Density at this height using exponential falloff
        float rayleighDensity = exp(-height / gRayleighScaleHeight) * gAtmosphereDensity;
        float mieDensity = exp(-height / gMieScaleHeight) * gAtmosphereDensity;
        
        opticalDepth.x += rayleighDensity * segmentLength;
        opticalDepth.y += mieDensity * segmentLength;
    }
    
    return opticalDepth;
}

// Ray-sphere intersection
// Returns distance to intersection, or -1 if no intersection
float2 RaySphereIntersect(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius)
{
    float3 oc = rayOrigin - sphereCenter;
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.0 * a * c;
    
    if (discriminant < 0.0)
        return float2(-1.0, -1.0);
    
    float sqrtD = sqrt(discriminant);
    float t1 = (-b - sqrtD) / (2.0 * a);
    float t2 = (-b + sqrtD) / (2.0 * a);
    
    return float2(t1, t2);
}

// Main atmospheric scattering calculation
float3 ComputeAtmosphericScattering(float3 rayOrigin, float3 rayDir)
{
    float3 planetCenter = float3(0.0, -gPlanetRadius, 0.0);
    
    // Find intersection with atmosphere
    float2 atmosphereIntersect = RaySphereIntersect(rayOrigin, rayDir, planetCenter, gAtmosphereRadius);
    
    if (atmosphereIntersect.y < 0.0)
        return float3(0.0, 0.0, 0.0); // No intersection with atmosphere
    
    // Check for planet intersection
    float2 planetIntersect = RaySphereIntersect(rayOrigin, rayDir, planetCenter, gPlanetRadius);
    
    float tMin = max(0.0, atmosphereIntersect.x);
    float tMax = atmosphereIntersect.y;
    
    // If ray hits planet, stop at planet surface
    if (planetIntersect.x > 0.0)
        tMax = min(tMax, planetIntersect.x);
    
    float rayLength = tMax - tMin;
    float segmentLength = rayLength / float(gNumSamples);
    
    float3 rayleighScattering = float3(0.0, 0.0, 0.0);
    float3 mieScattering = float3(0.0, 0.0, 0.0);
    float2 opticalDepthPA = float2(0.0, 0.0); // Optical depth from camera to current point
    
    float cosTheta = dot(rayDir, gSunDirection);
    float rayleighPhase = RayleighPhase(cosTheta);
    float miePhase = MiePhase(cosTheta, gMieAnisotropy);
    
    for (int i = 0; i < gNumSamples; ++i)
    {
        float3 samplePoint = rayOrigin + rayDir * (tMin + (float(i) + 0.5) * segmentLength);
        float height = length(samplePoint - planetCenter) - gPlanetRadius;
        
        // Density at sample point
        float rayleighDensity = exp(-height / gRayleighScaleHeight) * gAtmosphereDensity;
        float mieDensity = exp(-height / gMieScaleHeight) * gAtmosphereDensity;
        
        // Accumulate optical depth from camera to this point
        opticalDepthPA.x += rayleighDensity * segmentLength;
        opticalDepthPA.y += mieDensity * segmentLength;
        
        // Calculate light ray to sun
        float2 lightRayIntersect = RaySphereIntersect(samplePoint, gSunDirection, planetCenter, gAtmosphereRadius);
        float lightRayLength = lightRayIntersect.y;
        
        // Check if light ray is blocked by planet
        float2 lightPlanetIntersect = RaySphereIntersect(samplePoint, gSunDirection, planetCenter, gPlanetRadius);
        if (lightPlanetIntersect.x > 0.0 && lightPlanetIntersect.x < lightRayLength)
            continue; // Point is in shadow
        
        // Optical depth from sample point to sun
        float2 opticalDepthPS = ComputeOpticalDepth(samplePoint, gSunDirection, lightRayLength, gNumLightSamples);
        
        // Total optical depth
        float2 totalOpticalDepth = opticalDepthPA + opticalDepthPS;
        
        // Transmittance
        float3 transmittance = exp(-(gRayleighScattering * totalOpticalDepth.x + gMieScattering * totalOpticalDepth.y));
        
        // Accumulate scattering
        rayleighScattering += rayleighDensity * transmittance * segmentLength;
        mieScattering += mieDensity * transmittance * segmentLength;
    }
    
    // Final color
    float3 color = gSunIntensity * (rayleighScattering * gRayleighScattering * rayleighPhase +
                                     mieScattering * gMieScattering * miePhase);
    
    return color;
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    // Use local vertex position as direction vector
    vout.PosL = vin.PosL;
    
    // Transform to clip space, keeping at far plane
    float4 posW = float4(vin.PosL + gEyePosW, 1.0f);
    vout.PosH = mul(posW, gViewProj).xyww;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // Ray direction from camera through this pixel
    float3 rayDir = normalize(pin.PosL);
    
    // Camera position relative to planet center (planet center at origin, surface at gPlanetRadius)
    float3 rayOrigin = gCameraPositionKm + float3(0.0, gPlanetRadius + 0.001, 0.0);
    
    // Compute atmospheric scattering
    float3 color = ComputeAtmosphericScattering(rayOrigin, rayDir);
    
    // Apply exposure tone mapping
    color = 1.0 - exp(-gExposure * color);
    
    return float4(color, 1.0);
}

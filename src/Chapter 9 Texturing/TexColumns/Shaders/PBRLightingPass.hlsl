// PBRLightingPass.hlsl
//
// Deferred lighting pass (fullscreen triangle) with GGX PBR + IBL + skybox.
// Also supports velocity debug coloring via gVelocityDebugMode (b3).

#include "LightingUtil.hlsl"

// GBuffer
Texture2D gPositionMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gAlbedoMap : register(t2);

// Shadows / pattern (optional)
Texture2D gShadowMap : register(t3);
Texture2D gShadowTexture : register(t4);

// Velocity (for debug / TAA)
Texture2D<float2> gVelocityMap : register(t5);

// IBL + skybox
TextureCube gIrradianceMap : register(t6);
TextureCube gPrefilteredMap : register(t7);
Texture2D gBrdfLUT : register(t8);
TextureCube gSkyboxMap : register(t9);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

// Constant data that varies per frame (must match PassConstants layout).
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

    // TAA / velocity-related matrices (match PassConstants layout)
    float4x4 gViewProjNoJitter;
    float4x4 gPrevViewProjNoJitter;
    float4x4 gPrevViewProj;
    float2 gCurrJitterUV;
    float2 gPrevJitterUV;
    float2 _padTaa;

    Light gLights[MaxLights];
};

// Unused for fullscreen path, but kept for root signature compatibility.
cbuffer cbPerObject : register(b1)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    float4x4 gTexTransform;
};

cbuffer cbLight : register(b2)
{
    Light light;
};

cbuffer cbDebug : register(b3)
{
    // Root constants (4x32-bit): keep in sync with BuildLightingRootSignature (root param 9)
    uint gVelocityDebugMode;        // 0..3
    uint gUseDxrShadows;            // 0/1: if 1 and light.ShadowMode==1, read DXR shadow mask from t4
    uint gVisualizeDxrShadowMask;   // 0/1: output shadow mask as grayscale
    uint _padDebug;
};

cbuffer cbAtmosphere : register(b4)
{
    float3 gSunDirection;
    float gSunStrength;
    float gRayleigh;
    float gMie;
    float gTurbidity;
    float gBlendWithCubemap;
    float2 _padAtmosphere;
};

// Fullscreen triangle
struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VS_QUAD(uint vid : SV_VertexID)
{
    VSOut output;

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

// Debug VS for wireframe light shapes (not used by fullscreen pass).
struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 Tan : TANGENT;
};

VSOut VS(VertexIn vin)
{
    VSOut vout = (VSOut)0.0f;
    float4 posW = mul(float4(vin.PosL, 1.0f), light.gWorld);
    vout.PosH = mul(posW, gViewProj);
    vout.TexC = vin.TexC;
    return vout;
}

static const float PI = 3.14159265359f;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    return a2 / max(PI * denom * denom, 1e-6f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    float denom = NdotV * (1.0f - k) + k;
    return NdotV / max(denom, 1e-6f);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 oneMinusR = float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness);
    return F0 + (max(oneMinusR, F0) - F0) * pow(1.0f - cosTheta, 5.0f);
}

float3 ReconstructViewDirWS(int2 pix)
{
    float2 uv = (float2(pix) + 0.5f) * gInvRenderTargetSize;

    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    float4 clip = float4(ndc, 1.0f, 1.0f);
    float4 world = mul(clip, gInvViewProj);
    world.xyz /= max(world.w, 1e-6f);
    return normalize(world.xyz - gEyePosW);
}

// Infinite skybox: direction depends only on view rotation, not camera position.
// So the sky never "approaches" — it stays at infinity.
float3 ReconstructSkyboxViewDirWS(int2 pix)
{
    float2 uv = (float2(pix) + 0.5f) * gInvRenderTargetSize;
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;
    // View-space ray from camera (point on far plane in view space)
    float4 viewRay = mul(float4(ndc, 1.0f, 1.0f), gInvProj);
    viewRay.xyz /= max(viewRay.w, 1e-6f);
    float3 viewDirVS = normalize(viewRay.xyz);
    // World direction = rotate by invView (no translation)
    float3 viewDirWS = normalize(mul(float4(viewDirVS, 0.0f), gInvView).xyz);
    return viewDirWS;
}

// Seamless cubemap lookup (Castano-style): scale non-dominant axes so face edges align.
// Use with SampleLevel(..., 0) and pass the cube face size from GetDimensions.
float3 SeamlessCubeLookup(float3 v, float cubeFaceSize)
{
    float M = max(max(abs(v.x), abs(v.y)), abs(v.z));
    float scale = (cubeFaceSize - 1.0f) / cubeFaceSize;
    if (abs(v.x) != M) v.x *= scale;
    if (abs(v.y) != M) v.y *= scale;
    if (abs(v.z) != M) v.z *= scale;
    return normalize(v);
}

// Artist-friendly atmosphere: Rayleigh/Mie/Turbidity, optical path, sunset colors
float RayleighPhase(float cosTheta) { return 3.0f / (16.0f * PI) * (1.0f + cosTheta * cosTheta); }
float MiePhase(float cosTheta, float g) { float g2 = g * g; float denom = 1.0f + g2 - 2.0f * g * cosTheta; return (1.0f - g2) / (4.0f * PI * pow(max(denom, 1e-6f), 1.5f)); }

// Optical path: when sun is low, light travels through more atmosphere -> longer path -> more red
// sunElevation = sunDir.y (1 = zenith, 0 = horizon). Air mass ~ 1/sin(elevation).
void GetOpticalPathAndSunsetFactor(float3 sunDir, out float opticalPathNorm, out float sunsetFactor)
{
    float sunElevation = max(sunDir.y, 0.02f); // avoid div by zero
    float airMass = 1.0f / sunElevation;       // long path when sun low
    opticalPathNorm = saturate((airMass - 1.0f) / 15.0f);  // 0 at zenith, ~1 when sun near horizon
    sunsetFactor = 1.0f - exp(-opticalPathNorm * 2.0f);    // smooth 0..1 for sunset tint
}

float3 ComputeAtmosphereSky(float3 viewDir, float3 sunDir)
{
    float opticalPathNorm, sunsetFactor;
    GetOpticalPathAndSunsetFactor(sunDir, opticalPathNorm, sunsetFactor);

    float sunCos = saturate(dot(viewDir, sunDir));
    float height = saturate(viewDir.y * 0.5f + 0.5f);
    float horizon = 1.0f - height;

    // Horizon band: stronger near view horizon (viewDir.y small)
    float horizonBand = saturate(1.0f - viewDir.y * 2.0f);

    // Base gradient: blue zenith, warmer horizon; shift horizon to yellow/red when sun is low
    float3 zenithColor = float3(0.25f, 0.5f, 0.95f);
    float3 horizonColorDay = float3(0.7f, 0.8f, 0.95f);
    float3 horizonColorSunset = float3(1.0f, 0.55f, 0.2f);   // orange-red near horizon at sunset
    float3 horizonColor = lerp(horizonColorDay, horizonColorSunset, sunsetFactor * horizonBand);
    float3 skyGradient = lerp(horizonColor, zenithColor, height);

    // Rayleigh: blue scattered more when path is short; long path removes blue (red remains)
    float phaseR = RayleighPhase(sunCos);
    float rayleighAmount = gRayleigh * (1.0f + 0.3f * horizon);
    float3 rayleighTint = float3(0.3f, 0.5f, 1.0f);
    float rayleighPathAtten = 1.0f - opticalPathNorm * 0.7f; // less blue when path long
    float3 rayleighContribution = rayleighTint * phaseR * rayleighAmount * max(rayleighPathAtten, 0.2f);

    // Mie: haze + sun halo; longer path = more orange halo
    float phaseM = MiePhase(sunCos, -0.76f);
    float turbidityScale = 0.4f + 0.6f * gTurbidity;
    float mieAmount = gMie * turbidityScale * (0.6f + 0.4f * horizon) * (1.0f + 0.5f * opticalPathNorm);
    float3 mieHaze = float3(0.85f, 0.85f, 0.9f);
    float3 mieContribution = mieHaze * phaseM * mieAmount;
    float sunGlow = pow(sunCos, 4.0f) * gMie * gTurbidity * 2.0f;
    float3 mieSunHaloColor = lerp(float3(1.0f, 0.98f, 0.9f), float3(1.0f, 0.7f, 0.4f), sunsetFactor);
    float3 mieSunHalo = mieSunHaloColor * sunGlow;

    // Extra sunset glow along horizon band when sun is low
    float3 sunsetGlow = float3(1.0f, 0.5f, 0.15f) * sunsetFactor * horizonBand * (0.3f + 0.4f * sunCos);
    skyGradient += sunsetGlow;

    float hazeBlend = saturate((gMie * 0.5f + (gTurbidity - 1.0f) * 0.3f));
    float3 hazeGray = float3(0.6f, 0.65f, 0.75f);
    float3 skyWithScatter = skyGradient + rayleighContribution + mieContribution + mieSunHalo;
    float3 skyColor = lerp(skyWithScatter, hazeGray, hazeBlend * 0.5f);

    float sunScale = 0.4f + 0.6f * saturate(gSunStrength / 5.0f);
    return saturate(skyColor * sunScale);
}

float3 ComputeSunDisc(float3 viewDir, float3 sunDir, float radius, float intensity)
{
    float cosAngle = dot(viewDir, sunDir);
    float disc = smoothstep(radius - 0.002f, radius, cosAngle);

    float opticalPathNorm, sunsetFactor;
    GetOpticalPathAndSunsetFactor(sunDir, opticalPathNorm, sunsetFactor);
    // Sun color: white at zenith, yellow-orange-red near horizon (long optical path)
    float3 sunColorDay = float3(1.0f, 1.0f, 1.0f);
    float3 sunColorSunset = float3(1.0f, 0.6f, 0.2f);
    float3 sunColor = lerp(sunColorDay, sunColorSunset, sunsetFactor);
    return sunColor * intensity * disc;
}

// Poisson disk (unit circle); 12 samples for soft shadows (TAA will filter noise)
static const float2 kPoissonDisk12[12] = {
    float2(-0.326,-0.406), float2(-0.840,-0.074), float2(-0.696, 0.457),
    float2(-0.203, 0.621), float2( 0.962,-0.195), float2( 0.473,-0.480),
    float2( 0.519, 0.767), float2( 0.185,-0.893), float2( 0.507, 0.064),
    float2( 0.896, 0.412), float2(-0.322,-0.933), float2(-0.792,-0.598)
};

// Hash for per-pixel, per-frame rotation (TAA-friendly jitter)
float Hash21(float2 p)
{
    // Tie shadow jitter to the TAA jitter sequence so it changes once per frame in a stable way.
    float2 j = gCurrJitterUV * 16384.0f;
    return frac(sin(dot(p + j, float2(12.9898, 78.233))) * 43758.5453);
}

float ComputeShadowFactor(float3 posW, float2 pixelPos)
{
    // DXR shadow mask path: the mask is written by a RayQuery compute pass into t4 (gShadowTexture).
    if (gUseDxrShadows != 0 && light.CastsShadows)
    {
        // Map full-res pixel -> shadow mask texel and load (stable, avoids bilinear "shifts").
        uint mw, mh;
        gShadowTexture.GetDimensions(mw, mh);

        float2 uv = pixelPos * gInvRenderTargetSize; // 0..1
        int2 p = int2(uv * float2((float)mw, (float)mh));
        p = clamp(p, int2(0, 0), int2((int)mw - 1, (int)mh - 1));
        return gShadowTexture.Load(int3(p, 0)).r;
    }

    if (!light.CastsShadows)
        return 1.0f;
    if (!(light.type == 2 || light.type == 3))
        return 1.0f;

    uint shadowW, shadowH;
    gShadowMap.GetDimensions(shadowW, shadowH);
    float2 texelSize = 1.0f / max(float2(shadowW, shadowH), 1.0f);

    float4 shadowPosH = mul(float4(posW, 1.0f), light.LightViewProj);
    shadowPosH.xyz /= max(shadowPosH.w, 1e-6f);

    float2 shadowTexC;
    shadowTexC.x = 0.5f * shadowPosH.x + 0.5f;
    shadowTexC.y = -0.5f * shadowPosH.y + 0.5f;

    const float shadowBias = 0.0015f;

    float shadowFactor = 1.0f;
    if ((saturate(shadowTexC.x) == shadowTexC.x) && (saturate(shadowTexC.y) == shadowTexC.y) &&
        (shadowPosH.z > 0.0f) && (shadowPosH.z < 1.0f))
    {
        float softness = max(light.ShadowSoftness, 0.0f);

        if (softness > 0.0f)
        {
            // Soft shadows: poisson disk + random rotation; cone radius = softness (texels). TAA filters the noise.
            float angle = Hash21(pixelPos) * 6.28318530718;
            float s = sin(angle), c = cos(angle);
            float2 radius = softness * texelSize;
            float totalFactor = 0.0f;
            for (int i = 0; i < 12; i++)
            {
                float2 poisson = kPoissonDisk12[i];
                float2 offset = float2(poisson.x * c - poisson.y * s, poisson.x * s + poisson.y * c) * radius;
                totalFactor += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowTexC + offset, shadowPosH.z - shadowBias);
            }
            shadowFactor = totalFactor / 12.0f;
        }
        else if (light.enablePCF)
        {
            float totalFactor = 0.0f;
            int level = max(light.pcf_level, 0);
            for (int y = -level; y <= level; y++)
            {
                for (int x = -level; x <= level; x++)
                {
                    float2 offset = float2(x, y) * texelSize;
                    totalFactor += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowTexC + offset, shadowPosH.z - shadowBias);
                }
            }
            int n = (level * 2 + 1) * (level * 2 + 1);
            shadowFactor = totalFactor / max(n, 1);
        }
        else
        {
            shadowFactor = gShadowMap.SampleCmpLevelZero(gsamShadow, shadowTexC, shadowPosH.z - shadowBias);
        }
    }

    return shadowFactor;
}

float4 PS(VSOut pin) : SV_TARGET
{
    int2 pix = int2(pin.PosH.xy);

    // Read GBuffer
    float4 albedoRough = gAlbedoMap.Load(int3(pix, 0)); // rgb=baseColor, a=roughness
    float4 normalMet = gNormalMap.Load(int3(pix, 0));   // xyz=normal, a=metallic
    float3 posW = gPositionMap.Load(int3(pix, 0)).xyz;

    float3 normalRaw = normalMet.xyz;
    bool hasGeom = dot(normalRaw, normalRaw) > 1e-8f;

    // Background: infinite skybox = atmosphere + sun disc, blended with cubemap
    if (!hasGeom)
    {
        if (light.type == 0)
        {
            float3 viewDirWS = ReconstructSkyboxViewDirWS(pix);
            // Light direction in CB points FROM sun TO scene (down); we need direction TOWARD sun (up)
            float3 sunDir = normalize(-gSunDirection);

            float3 atmosphereSky = ComputeAtmosphereSky(viewDirWS, sunDir);
            float3 sunDisc = ComputeSunDisc(viewDirWS, sunDir, 0.998f, min(gSunStrength * 0.8f, 3.0f));
            float3 atmosphereTotal = atmosphereSky + sunDisc;

            uint cubeW, cubeH, cubeMips;
            gSkyboxMap.GetDimensions(0, cubeW, cubeH, cubeMips);
            float faceSize = (float)min(cubeW, cubeH);
            if (faceSize < 1.0f) faceSize = 1.0f;
            float3 seamFixedDir = SeamlessCubeLookup(viewDirWS, faceSize);
            float3 cubemapSky = gSkyboxMap.SampleLevel(gsamLinearClamp, seamFixedDir, 0).rgb;

            float3 sky = lerp(atmosphereTotal, cubemapSky, gBlendWithCubemap);
            return float4(sky, 1.0f);
        }

        return float4(0, 0, 0, 0);
    }

    float3 baseColor = albedoRough.rgb;
    float roughness = saturate(albedoRough.a);
    float metallic = saturate(normalMet.a);

    // Clamp to avoid fireflies / divide-by-zero
    roughness = clamp(roughness, 0.04f, 1.0f);
    metallic = clamp(metallic, 0.0f, 1.0f);

    // Optional debug coloring using velocity buffer (modes 2/3).
    if (gVelocityDebugMode == 2 || gVelocityDebugMode == 3)
    {
        float2 fullVelUV = gVelocityMap.Load(int3(pix, 0));
        float2 velUV = fullVelUV;

        if (gVelocityDebugMode == 3)
        {
            float4 currClip = mul(float4(posW, 1.0f), gViewProjNoJitter);
            float4 prevClip = mul(float4(posW, 1.0f), gPrevViewProjNoJitter);

            float invWc = (abs(currClip.w) > 1e-6f) ? (1.0f / currClip.w) : 0.0f;
            float invWp = (abs(prevClip.w) > 1e-6f) ? (1.0f / prevClip.w) : 0.0f;

            float2 currNdc = currClip.xy * invWc;
            float2 prevNdc = prevClip.xy * invWp;
            float2 camVelNdc = prevNdc - currNdc;
            float2 camVelUV = camVelNdc * float2(0.5f, -0.5f);

            velUV = fullVelUV - camVelUV;
        }

        if (dot(velUV, velUV) > 1e-10f)
        {
            baseColor = float3(1.0f, 0.0f, 0.0f);
        }
    }

    float3 N = normalize(normalRaw);
    float3 V = normalize(gEyePosW - posW);
    float NdotV = saturate(dot(N, V));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);

    float shadowFactor = ComputeShadowFactor(posW, pin.PosH.xy);
    if (gVisualizeDxrShadowMask != 0)
        return float4(shadowFactor.xxx, 1.0f);

    // Ambient / IBL pass (computed once).
    if (light.type == 0)
    {
        float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kd = (1.0f - F) * (1.0f - metallic);

        float3 irradiance = gIrradianceMap.Sample(gsamLinearClamp, N).rgb;
        float3 diffuseIBL = irradiance * baseColor;

        // Reflection direction: outgoing from surface (D3D cubemap = direction from center outward)
        // Use R as-is; if env was captured as "incoming", sample with -R to fix flipped reflection
        float3 R = reflect(-V, N);
        float3 R_sample = -R; // fix flipped reflection in spheres (cubemap convention)
        uint w, h, mipLevels;
        gPrefilteredMap.GetDimensions(0, w, h, mipLevels);
        float maxMip = (mipLevels > 0) ? (float)(mipLevels - 1) : 0.0f;
        float3 prefilteredColor = gPrefilteredMap.SampleLevel(gsamLinearClamp, R_sample, roughness * maxMip).rgb;

        float2 brdf = gBrdfLUT.Sample(gsamLinearClamp, float2(NdotV, roughness)).rg;
        float3 specIBL = prefilteredColor * (F * brdf.x + brdf.y);

        // Add sun reflection so the light source appears in spheres (toward sun = -light dir)
        float3 sunDir = normalize(-gSunDirection);
        float3 sunInReflection = ComputeSunDisc(R_sample, sunDir, 0.995f, gSunStrength * 0.5f);
        specIBL += sunInReflection;

        float ambientScale = max(light.Strength, 0.0f);
        float3 color = (kd * diffuseIBL + specIBL) * ambientScale;
        return float4(color, 1.0f);
    }

    // Direct lights
    float3 L = float3(0.0f, 0.0f, 0.0f);
    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    if (light.type == 1)
    {
        float3 toLight = light.Position - posW;
        float dist = length(toLight);
        if (dist > 1e-4f)
            L = toLight / dist;

        float att = CalcAttenuation(dist, light.FalloffStart, light.FalloffEnd);
        radiance = light.Color * light.Strength * att;
    }
    else if (light.type == 2)
    {
        L = normalize(-light.Direction);
        radiance = light.Color * light.Strength * shadowFactor;
    }
    else if (light.type == 3)
    {
        float3 toLight = light.Position - posW;
        float dist = length(toLight);
        if (dist > 1e-4f)
            L = toLight / dist;

        float att = CalcAttenuation(dist, light.FalloffStart, light.FalloffEnd);
        float3 dir = normalize(light.Direction);
        float spot = pow(saturate(dot(-L, dir)), light.SpotPower);
        radiance = light.Color * light.Strength * att * spot * shadowFactor;
    }
    else
    {
        return float4(0, 0, 0, 0);
    }

    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0f)
        return float4(0, 0, 0, 0);

    float3 H = normalize(V + L);

    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

    float3 numerator = D * G * F;
    float denom = 4.0f * max(NdotV, 1e-6f) * max(NdotL, 1e-6f);
    float3 specular = numerator / max(denom, 1e-6f);

    float3 kd = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kd * baseColor / PI;

    float3 Lo = (diffuse + specular) * radiance * NdotL;
    return float4(Lo, 1.0f);
}

float4 PS_debug(VSOut pin) : SV_TARGET
{
    return float4(1, 1, 1, 1);
}


// DxrShadowMask.hlsl
// DXR 1.1 inline ray tracing (RayQuery) soft shadow mask for ONE directional light.
//
// Output: gShadowMask(x,y) in [0..1] where 1=lit, 0=occluded.
// Soft shadows: jitter ray direction inside a cone and average. TAA will filter noise over frames.

RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float4> gPositionMap : register(t1); // xyz = world position
Texture2D<float4> gNormalMap   : register(t2); // xyz = world normal (encoded), a = metallic

RWTexture2D<float> gShadowMask : register(u0);

cbuffer cbDxrShadow : register(b0)
{
    float3 gLightDirW;      // direction of light rays (from light -> scene), normalized
    float  gConeAngleRad;   // half-angle of cone (radians). 0 = hard.

    uint   gSampleCount;    // e.g. 1..16
    uint   gFrameIndex;     // increments every frame
    uint   gDownscale;      // 1=full,2=half,4=quarter
    uint   _pad0;
    float  gMaxDistance;    // ray length
    float  gNormalBias;     // origin offset along normal
    float2 _pad1;
};

// Simple hash (pixel + frame) for rotation/jitter
float Hash21(float2 p, uint frame)
{
    float h = dot(p, float2(12.9898, 78.233)) + (float)frame * 0.6180339887;
    return frac(sin(h) * 43758.5453);
}

// Build orthonormal basis around direction.
void BuildBasis(float3 n, out float3 t, out float3 b)
{
    float3 up = (abs(n.y) < 0.999f) ? float3(0, 1, 0) : float3(1, 0, 0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// Sample direction inside a cone around dir (small-angle approximation using tangent plane offsets).
float3 JitterConeDir(float3 dir, float2 xi, float coneAngle)
{
    if (coneAngle <= 0.0f) return dir;

    float3 t, b;
    BuildBasis(dir, t, b);

    // Disk sample (concentric mapping would be nicer; this is OK for small angles)
    float r = sqrt(xi.x);
    float phi = 6.28318530718f * xi.y;
    float2 disk = r * float2(cos(phi), sin(phi));

    float k = tan(coneAngle);
    float3 d = normalize(dir + t * (disk.x * k) + b * (disk.y * k));
    return d;
}

[numthreads(8, 8, 1)]
void CS(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pix = dtid.xy;

    uint w, h;
    gShadowMask.GetDimensions(w, h);
    if (pix.x >= w || pix.y >= h) return;

    uint fw, fh;
    gPositionMap.GetDimensions(fw, fh);
    uint s = max(gDownscale, 1u);
    uint2 fullPix = uint2(min(pix.x * s + s / 2, fw - 1), min(pix.y * s + s / 2, fh - 1));

    float3 posW = gPositionMap.Load(int3(fullPix, 0)).xyz;
    float3 nrm  = gNormalMap.Load(int3(fullPix, 0)).xyz;

    // If background / empty GBuffer.
    if (dot(nrm, nrm) < 1e-8f)
    {
        gShadowMask[pix] = 1.0f;
        return;
    }

    float3 N = normalize(nrm);
    float3 dirToLight = normalize(-gLightDirW); // toward light source

    float3 origin = posW + N * gNormalBias;
    float tMax = max(gMaxDistance, 0.0f);

    uint samples = max(gSampleCount, 1u);
    float visible = 0.0f;

    // Stratified-ish seeds from pixel + frame.
    for (uint i = 0; i < samples; ++i)
    {
        float2 seed = float2(pix) + float2(i * 17.0f, i * 29.0f);
        float u1 = Hash21(seed + 0.13f, gFrameIndex);
        float u2 = Hash21(seed + 0.73f, gFrameIndex);

        float3 rayDir = JitterConeDir(dirToLight, float2(u1, u2), gConeAngleRad);

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = rayDir;
        ray.TMin = 0.001f;
        ray.TMax = tMax;

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rq;
        rq.TraceRayInline(gScene,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
            0xFF, ray);

        while (rq.Proceed()) { }

        bool hit = (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
        visible += hit ? 0.0f : 1.0f;
    }

    gShadowMask[pix] = visible / (float)samples;
}



Texture2D CurrentTex : register(t0);
Texture2D HistoryTex : register(t1);
Texture2D DepthTex : register(t2);
Texture2D PrevDepthTex : register(t3);
Texture2D VelocityTex : register(t4);
SamplerState LinearClamp : register(s0);

cbuffer TAAConstants : register(b0)
{
    float gAlpha;
    float gClampExpand;
    float2 gInvRTSize;
};


cbuffer TAAReprojectCB : register(b1)
{
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj; 
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    float2 p = positions[vid];
    o.PosH = float4(p, 0.0f, 1.0f);

    o.UV = float2((o.PosH.x + 1.0f) * 0.5f, (1.0f - o.PosH.y) * 0.5f);

    return o;
}

float2 ReprojectUV(float2 uv)
{
    float depth = DepthTex.SampleLevel(LinearClamp, uv, 0).r;

    if (depth >= 0.999999f)
        return float2(-1.0f, -1.0f);


    float2 ndcXY;
    ndcXY.x = uv.x * 2.0f - 1.0f;
    ndcXY.y = 1.0f - uv.y * 2.0f; //инверси€ Y

    float4 currClip = float4(ndcXY, depth, 1.0f);

    float4 world = mul(currClip, gInvViewProj);
    world.xyz /= world.w;

    float4 prevClip = mul(float4(world.xyz, 1.0f), gPrevViewProj);
    if (prevClip.w <= 1e-6f)
        return float2(-1.0f, -1.0f);

    float2 prevNdc = prevClip.xy / prevClip.w;

    float2 prevUV;
    prevUV.x = prevNdc.x * 0.5f + 0.5f;
    prevUV.y = 0.5f - prevNdc.y * 0.5f;

    return prevUV;
}


float3 NeighborhoodMin(float2 uv)
{
    float3 mn = float3(1e9, 1e9, 1e9);
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 o = float2(x, y) * gInvRTSize;
            float3 c = CurrentTex.SampleLevel(LinearClamp, uv + o, 0).rgb;
            mn = min(mn, c);
        }
    }
    return mn;
}

float3 NeighborhoodMax(float2 uv)
{
    float3 mx = float3(-1e9, -1e9, -1e9);
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 o = float2(x, y) * gInvRTSize;
            float3 c = CurrentTex.SampleLevel(LinearClamp, uv + o, 0).rgb;
            mx = max(mx, c);
        }
    }
    return mx;
}

float4 PS(VSOut i) : SV_Target
{
    float2 uv = i.UV;

    float3 cur = CurrentTex.SampleLevel(LinearClamp, uv, 0).rgb;

    
    
    float2 v = VelocityTex.SampleLevel(LinearClamp, uv, 0).xy;
    float2 prevUV = uv + v;

    
    
    // если невалидно Ч сбрасываем history
    if (prevUV.x < 0.0f || prevUV.x > 1.0f || prevUV.y < 0.0f || prevUV.y > 1.0f)
        return float4(cur, 1.0f);

    float depthCur = DepthTex.SampleLevel(LinearClamp, uv, 0).r;
    float depthPrev = PrevDepthTex.SampleLevel(LinearClamp, prevUV, 0).r;

    float dz = abs(depthPrev - depthCur);


    float depthReject = saturate((dz - 0.0005f) * 2000.0f);

// вместо выкинуть history можно просто подн€ть alpha
    float alpha = lerp(gAlpha, 1.0f, depthReject);
    
    
    float3 prev = HistoryTex.SampleLevel(LinearClamp, prevUV, 0).rgb;

    // 3x3 clamp box по текущему кадру
    float3 mn = float3(1e9, 1e9, 1e9);
    float3 mx = float3(-1e9, -1e9, -1e9);

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 o = float2(x, y) * gInvRTSize;
            float3 c = CurrentTex.SampleLevel(LinearClamp, uv + o, 0).rgb;
            mn = min(mn, c);
            mx = max(mx, c);
        }
    }

    //чтобы не было мерцаний/пережима
    float3 ext = (mx - mn) * gClampExpand;
    mn -= ext;
    mx += ext;

    float3 prevClamped = clamp(prev, mn, mx);
    float3 outc = lerp(prevClamped, cur, alpha);
    //float3 outc = prev; // только истори€
    return float4(outc, 1);
    

}

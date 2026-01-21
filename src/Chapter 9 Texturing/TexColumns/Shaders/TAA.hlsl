Texture2D prevFrame : register(t0);
Texture2D currFrame : register(t1);
Texture2D VelocityTexture : register(t2);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

cbuffer TAAConstants : register(b0)
{
    float blendFactor;
    float3 padding;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

struct PSOut
{
    float4 BackBuffer : SV_Target0;
    float4 HistoryTexture : SV_Target1;
};

VSOut VS(uint vid : SV_VertexID)
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
    output.TexC.y = 1.0 - output.TexC.y;
    return output;
}



PSOut PS(VSOut pin) : SV_Target
{
    PSOut pout;
    float2 velocity = VelocityTexture.Sample(gsamPointClamp, pin.TexC).xy;
    float motion = length(velocity);
    float2 prevTexC = pin.TexC - velocity;
    float4 historyColor = prevFrame.Sample(gsamLinearClamp, prevTexC);
    float4 currentColor = currFrame.Sample(gsamPointClamp, pin.TexC);
    
    float4 NearColor0 = currFrame.Sample(gsamLinearWrap, pin.TexC, int2(1, 0));
    float4 NearColor1 = currFrame.Sample(gsamLinearWrap, pin.TexC, int2(0, 1));
    float4 NearColor2 = currFrame.Sample(gsamLinearWrap, pin.TexC, int2(-1, 0));
    float4 NearColor3 = currFrame.Sample(gsamLinearWrap, pin.TexC, int2(0, -1));
    
    float4 BoxMin = min(currentColor, min(NearColor0, min(NearColor1, min(NearColor2, NearColor3))));
    float4 BoxMax = max(currentColor, max(NearColor0, max(NearColor1, max(NearColor2, NearColor3))));
    
    historyColor = clamp(historyColor, BoxMin, BoxMax);
    
    float motionFactor = saturate(motion * 100.0f); // масштаб под разрешение
    float adaptiveBlend = lerp(blendFactor, 1.0f, motionFactor);

    
    pout.BackBuffer = lerp(historyColor, currentColor, adaptiveBlend);
    pout.HistoryTexture = lerp(historyColor, currentColor, adaptiveBlend);
    return pout;
    
}
// Terrain.hlsl - Шейдеры для рендеринга terrain с heightmap

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    float4x4 gTexTransform;
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

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};
cbuffer cbTerrainTile : register(b3) // b1 - регистр для буфера
{
    float3 gTilePosition;
    float gTileSize;
    float mapSize;
    float heightScale;
    float showborders;
    float debugMode;
};
// Texture resources
Texture2D gHeightMap : register(t0); // Карта высот
Texture2D gDiffuseMap : register(t1); // Диффузная текстура
Texture2D gNormalMap : register(t2); // Карта нормалей

SamplerState gSamPointWrap : register(s0);
SamplerState gSamPointClamp : register(s1);
SamplerState gSamLinearWrap : register(s2);
SamplerState gSamLinearClamp : register(s3);
SamplerState gSamAnisotropicWrap : register(s4);
SamplerState gSamAnisotropicClamp : register(s5);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
    float2 TexCl : TEXCOORD2;
};

struct PixelOut
{
    float4 Albedo : SV_Target0; // Диффузный цвет
    float4 Normal : SV_Target1; // Нормали в мировом пространстве
    float4 Position : SV_Target2; // Позиция в мировом пространстве
};

// Константы для terrain
static const float TEXTURE_REPEAT = 3.0f; // Повторение текстуры на тайле
static const float NORMAL_SAMPLE_OFFSET = 0.01f; // Смещение для вычисления нормалей

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
    
    // Вычисляем текстурные координаты
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;
    float coeff = gTileSize / mapSize;
    vout.TexC *= coeff;
    vout.TexC += gTilePosition.xz / mapSize;
    vout.TexCl = vin.TexC;
    // Семплируем высоту из heightmap
    float height = gHeightMap.SampleLevel(gSamLinearClamp, vout.TexC, 0).r;
    
    // Применяем высоту к Y координате
    float3 posL = vin.PosL;
    posL.y = posL.y + height * heightScale;
    
    // Трансформируем в мировые координаты
    float4 posW = mul(float4(posL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    
    // ИСПРАВЛЕННОЕ вычисление нормали
    float2 texelSize = float2(1.0f / mapSize, 1.0f / mapSize);
    
    // Семплируем высоты соседних точек
    float hL = gHeightMap.SampleLevel(gSamLinearClamp, vout.TexC + float2(-texelSize.x, 0.0f), 0).r;
    float hR = gHeightMap.SampleLevel(gSamLinearClamp, vout.TexC + float2(texelSize.x, 0.0f), 0).r;
    float hD = gHeightMap.SampleLevel(gSamLinearClamp, vout.TexC + float2(0.0f, -texelSize.y), 0).r;
    float hU = gHeightMap.SampleLevel(gSamLinearClamp, vout.TexC + float2(0.0f, texelSize.y), 0).r;
    
    // Вычисляем градиенты
    float dX = (hR - hL) * heightScale ; // градиент по X
    float dZ = (hU - hD) * heightScale ; // градиент по Z
    
    // Создаем нормаль напрямую из градиентов
    // Формула: normal = normalize((-dX, 1, -dZ))
    float3 normal = normalize(float3(-dX, 1.0f, -dZ));
    
    // Создаем тангент
    float3 tangent = normalize(float3(1.0f, dX, 0.0f));
    
    // Трансформируем в мировое пространство
    vout.NormalW = normalize(mul(normal, (float3x3) gWorld));
    vout.TangentW = normalize(mul(tangent, (float3x3) gWorld));
    
    // Трансформируем в clip space
    vout.PosH = mul(posW, gViewProj);
    
    return vout;
}
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    // Распаковываем нормаль из [0,1] в [-1,1]
    float3 normalT = 2.0f * normalMapSample - 1.0f;
    
    // Строим TBN матрицу
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);
    
    float3x3 TBN = float3x3(T, B, N);
    
    // Трансформируем нормаль в мировое пространство
    float3 bumpedNormalW = mul(normalT, TBN);
    
    return bumpedNormalW;
}

PixelOut PS(VertexOut pin) : SV_Target
{
    PixelOut pout;
    
    // Семплируем диффузную текстуру
    float4 diffuseAlbedo = gDiffuseMap.Sample(gSamAnisotropicWrap, pin.TexC);
    
    // Применяем материальный цвет
    diffuseAlbedo *= gDiffuseAlbedo;
    
    // debug
    
    if (debugMode)
    {
        if (gTileSize == mapSize)
            diffuseAlbedo = float4(0.5f, 0.0f, 0.0f, 1.0f);
        else if (gTileSize == mapSize / 2)
            diffuseAlbedo = float4(0.8f, 0.3f, 0.0f, 1.0f);
        else if (gTileSize == mapSize / 4)
            diffuseAlbedo = float4(1.0f, 0.7f, 0.0f, 1.0f);
        else if (gTileSize == mapSize / 8)
            diffuseAlbedo = float4(0.f, 0.3f, 0.1f, 1.0f);
        else if (gTileSize == mapSize / 16)
            diffuseAlbedo = float4(0.1f, 0.5f, 0.1f, 1.0f);
        else if (gTileSize == mapSize / 32)
            diffuseAlbedo = float4(0.0f, 1.0f, 0.3f, 1.0f);
        else if (gTileSize == mapSize / 64)
            diffuseAlbedo = float4(0.0f, 1.0f, 1.0f, 1.0f);
    }
        
    if (showborders)
    {
        float2 uv = pin.TexCl;
        if (uv.x < 0.005 || uv.x > 0.995 || uv.y < 0.005 || uv.y > 0.995)
            diffuseAlbedo = float4(0, 0, 0, 0);
    }
   
    
    
    // Семплируем карту нормалей
        float3 normalMapSample = gNormalMap.Sample(gSamAnisotropicWrap, pin.TexC).rgb;
    
    // Нормализуем интерполированные нормали и тангенты
    pin.NormalW = normalize(pin.NormalW);
    pin.TangentW = normalize(pin.TangentW);
    
    // Вычисляем финальную нормаль с учетом normal map
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample, pin.NormalW, pin.TangentW);
    
    // Выводим в G-Buffer
    
    
    pout.Albedo = diffuseAlbedo;
    pout.Normal = float4(bumpedNormalW, gRoughness);
    pout.Position = float4(pin.PosW, 1.0f);
    
    return pout;
}

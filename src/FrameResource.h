#pragma once

#include "Common/d3dUtil.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 m_worldTransform = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 m_textureTransform = MathHelper::Identity4x4();

    unsigned int m_materialSlot = 0;
    unsigned int m_detailLevel = 0;

    unsigned int m_paddingA = 0;
    unsigned int m_paddingB = 0;
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 m_viewMatrix = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 m_viewInverse = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 m_projectionMatrix = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 m_projectionInverse = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 m_viewProjection = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 m_viewProjectionInverse = MathHelper::Identity4x4();

    DirectX::XMFLOAT3 m_cameraPosition = { 0.0f, 0.0f, 0.0f };
    float m_alignmentA = 0.0f;

    DirectX::XMFLOAT2 m_targetDimensions = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 m_targetDimensionsInv = { 0.0f, 0.0f };

    float m_planeNear = 0.0f;
    float m_planeFar = 0.0f;
    float m_accumulatedTime = 0.0f;
    float m_frameDelta = 0.0f;

    DirectX::XMFLOAT4 m_ambientRadiance = { 0.0f, 0.0f, 0.0f, 1.0f };

    DirectX::XMFLOAT4 m_frustumBoundaries[6] = {};

    Light m_sceneLights[MaxLights] = {};
};

struct TerrainConstants
{
    float m_elevationMinimum = 0.0f;
    float m_elevationMaximum = 100.0f;
    float m_terrainExtent = 1024.0f;
    float m_texelSpacing = 1.0f / 1024.0f;

    DirectX::XMFLOAT2 m_heightfieldResolution = { 1024.0f, 1024.0f };
    DirectX::XMFLOAT2 m_reservedSpace = { 0.0f, 0.0f };
};

struct MaterialData
{
    DirectX::XMFLOAT4 m_baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 m_reflectivity = { 0.01f, 0.01f, 0.01f };
    float m_surfaceRoughness = 0.5f;

    DirectX::XMFLOAT4X4 m_uvTransform = MathHelper::Identity4x4();

    unsigned int m_albedoTextureId = 0;
    unsigned int m_normalTextureId = 0;

    unsigned int m_paddingC = 0;
    unsigned int m_paddingD = 0;
};

struct Vertex
{
    DirectX::XMFLOAT3 m_localPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_localNormal = { 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT2 m_texCoord = { 0.0f, 0.0f };
};

struct FrameResource
{
public:
    FrameResource(ID3D12Device* pDevice, unsigned int nPasses, unsigned int nMaxObjects, unsigned int nMaterials);
    FrameResource(const FrameResource&) = delete;
    FrameResource& operator=(const FrameResource&) = delete;
    ~FrameResource();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocator;

    std::unique_ptr<UploadBuffer<PassConstants>> m_passConstants = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> m_objectConstants = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> m_materialData = nullptr;
    std::unique_ptr<UploadBuffer<TerrainConstants>> m_terrainConstants = nullptr;

    unsigned long long m_syncValue = 0;
};
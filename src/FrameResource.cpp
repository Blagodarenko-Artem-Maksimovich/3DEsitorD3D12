#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* pDevice, UINT nPasses, UINT nMaxObjects, UINT nMaterials)
{
    pDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_allocator));

    m_passConstants = std::make_unique<UploadBuffer<PassConstants>>(pDevice, nPasses, true);
    m_objectConstants = std::make_unique<UploadBuffer<ObjectConstants>>(pDevice, nMaxObjects, true);
    m_materialData = std::make_unique<UploadBuffer<MaterialData>>(pDevice, nMaterials, false);
    m_terrainConstants = std::make_unique<UploadBuffer<TerrainConstants>>(pDevice, 1, true);
}

FrameResource::~FrameResource()
{
}
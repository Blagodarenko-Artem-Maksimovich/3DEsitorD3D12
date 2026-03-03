#pragma once

#include "Common/d3dUtil.h"
#include "Common/MathHelper.h"
#include "QuadTree.h"

#include <vector>
#include <memory>

struct TerrainVertex
{
    DirectX::XMFLOAT3 m_localPosition;
    DirectX::XMFLOAT3 m_localNormal;
    DirectX::XMFLOAT2 m_textureCoordinate;
};

class Terrain
{
public:
    Terrain(ID3D12Device* graphicsDevice,
        ID3D12GraphicsCommandList* commandList,
        float worldExtent,
        float baseElevation,
        float peakElevation);

    ~Terrain() = default;

    bool LoadHeightmap(const std::wstring& filePath,
        unsigned int width,
        unsigned int height,
        bool sixteenBit = true);

    bool LoadHeightmapDDS(const std::wstring& filePath,
        ID3D12Device* graphicsDevice,
        ID3D12GraphicsCommandList* commandList);

    void GenerateProceduralHeightmap(unsigned int width,
        unsigned int height,
        float baseFrequency,
        int octaveCount);

    void BuildGeometry(ID3D12Device* graphicsDevice, ID3D12GraphicsCommandList* commandList);

    float GetHeight(float worldX, float worldZ) const;
    DirectX::XMFLOAT3 GetNormal(float worldX, float worldZ) const;

    float GetTerrainSize() const { return m_worldExtent; }
    float GetMinHeight() const { return m_baseElevation; }
    float GetMaxHeight() const { return m_peakElevation; }
    unsigned int GetHeightmapWidth() const { return m_heightfieldWidth; }
    unsigned int GetHeightmapHeight() const { return m_heightfieldHeight; }

    MeshGeometry* GetGeometry() { return m_meshData.get(); }
    ID3D12Resource* GetHeightmapResource() { return m_heightfieldTexture.Get(); }

    static const char* GetLODMeshIdentifier(int lodIndex);

private:
    float FetchHeightSample(int xCoord, int zCoord) const;

    float PerlinNoise(float xCoord, float zCoord) const;
    float Smoothstep(float t) const;
    float Interpolate(float a, float b, float t) const;
    float Gradient(int hashValue, float xComponent, float zComponent) const;

private:
    ID3D12Device* m_graphicsDevice = nullptr;

    float m_worldExtent = 0.0f;
    float m_baseElevation = 0.0f;
    float m_peakElevation = 0.0f;

    unsigned int m_heightfieldWidth = 0;
    unsigned int m_heightfieldHeight = 0;

    std::vector<float> m_heightfield;

    std::unique_ptr<MeshGeometry> m_meshData;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_heightfieldTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_heightfieldUploadBuffer;

    std::vector<int> m_permutationTable;
};
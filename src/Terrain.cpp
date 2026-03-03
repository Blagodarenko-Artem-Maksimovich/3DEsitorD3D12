#include "Terrain.h"
#include "Common/DDSTextureLoader.h"

#include <fstream>
#include <random>
#include <cmath>
#include <algorithm>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

const char* Terrain::GetLODMeshIdentifier(int lodIndex)
{
    static const char* identifiers[5] = { "lod0", "lod1", "lod2", "lod3", "lod4" };
    if (lodIndex < 0 || lodIndex >= 5) return identifiers[0];
    return identifiers[lodIndex];
}

Terrain::Terrain(ID3D12Device* graphicsDevice,
    ID3D12GraphicsCommandList* /*commandList*/,
    float worldExtent,
    float baseElevation,
    float peakElevation)
    : m_graphicsDevice(graphicsDevice)
    , m_worldExtent(worldExtent)
    , m_baseElevation(baseElevation)
    , m_peakElevation(peakElevation)
{
    std::vector<int> baseIndices(256);
    for (int i = 0; i < 256; ++i) baseIndices[i] = i;

    std::random_device randomSource;
    std::mt19937 generator(randomSource());
    std::shuffle(baseIndices.begin(), baseIndices.end(), generator);

    m_permutationTable.assign(512, 0);
    for (int i = 0; i < 256; ++i)
    {
        m_permutationTable[i] = baseIndices[i];
        m_permutationTable[i + 256] = baseIndices[i];
    }
}

bool Terrain::LoadHeightmap(const std::wstring& filePath, unsigned int width, unsigned int height, bool sixteenBit)
{
    std::ifstream inputStream(filePath, std::ios::binary);
    if (!inputStream) return false;

    m_heightfieldWidth = width;
    m_heightfieldHeight = height;
    m_heightfield.assign((size_t)width * (size_t)height, 0.0f);

    size_t elementCount = (size_t)width * (size_t)height;

    if (sixteenBit)
    {
        std::vector<std::uint16_t> rawData(elementCount);
        inputStream.read(reinterpret_cast<char*>(rawData.data()), (std::streamsize)(elementCount * sizeof(std::uint16_t)));

        for (size_t i = 0; i < elementCount; ++i)
            m_heightfield[i] = rawData[i] / 65535.0f;
    }
    else
    {
        std::vector<std::uint8_t> rawData(elementCount);
        inputStream.read(reinterpret_cast<char*>(rawData.data()), (std::streamsize)(elementCount * sizeof(std::uint8_t)));

        for (size_t i = 0; i < elementCount; ++i)
            m_heightfield[i] = rawData[i] / 255.0f;
    }

    return true;
}

bool Terrain::LoadHeightmapDDS(const std::wstring& filePath, ID3D12Device* graphicsDevice, ID3D12GraphicsCommandList* commandList)
{
    HRESULT result = DirectX::CreateDDSTextureFromFile12(
        graphicsDevice, commandList, filePath.c_str(),
        m_heightfieldTexture, m_heightfieldUploadBuffer);

    if (FAILED(result))
        return false;

    D3D12_RESOURCE_DESC resourceDetails = m_heightfieldTexture->GetDesc();
    m_heightfieldWidth = (unsigned int)resourceDetails.Width;
    m_heightfieldHeight = resourceDetails.Height;

    m_heightfield.assign((size_t)m_heightfieldWidth * (size_t)m_heightfieldHeight, 0.0f);

    for (unsigned int z = 0; z < m_heightfieldHeight; ++z)
    {
        for (unsigned int x = 0; x < m_heightfieldWidth; ++x)
        {
            float fx = (float)x / (float)m_heightfieldWidth;
            float fz = (float)z / (float)m_heightfieldHeight;

            float noiseValue = PerlinNoise(fx * 4.0f, fz * 4.0f);
            m_heightfield[(size_t)z * (size_t)m_heightfieldWidth + x] = noiseValue * 0.5f + 0.5f;
        }
    }

    return true;
}

void Terrain::GenerateProceduralHeightmap(unsigned int width, unsigned int height, float baseFrequency, int octaveCount)
{
    m_heightfieldWidth = width;
    m_heightfieldHeight = height;
    m_heightfield.assign((size_t)width * (size_t)height, 0.0f);

    float highest = 0.0f;
    float lowest = 1.0f;

    for (unsigned int z = 0; z < height; ++z)
    {
        for (unsigned int x = 0; x < width; ++x)
        {
            float nx = (float)x / (float)width;
            float nz = (float)z / (float)height;

            float accumulated = 0.0f;
            float amplitude = 1.0f;
            float frequency = baseFrequency;
            float amplitudeSum = 0.0f;

            for (int o = 0; o < octaveCount; ++o)
            {
                accumulated += PerlinNoise(nx * frequency, nz * frequency) * amplitude;
                amplitudeSum += amplitude;

                amplitude *= 0.5f;
                frequency *= 2.0f;
            }

            float value = (accumulated / amplitudeSum + 1.0f) * 0.5f;
            m_heightfield[(size_t)z * (size_t)width + x] = value;

            highest = (std::max)(highest, value);
            lowest = (std::min)(lowest, value);
        }
    }

    float range = highest - lowest;
    if (range > 0.001f)
    {
        for (float& sample : m_heightfield)
            sample = (sample - lowest) / range;
    }
}

void Terrain::BuildGeometry(ID3D12Device* graphicsDevice, ID3D12GraphicsCommandList* commandList)
{
    m_meshData = std::make_unique<MeshGeometry>();
    m_meshData->Name = "terrainGeo";

    std::vector<TerrainVertex> vertexCollection;
    std::vector<std::uint32_t> indexCollection;

    const unsigned int gridDimensions[5] = { 256, 128, 64, 32, 16 };

    for (int lod = 0; lod < 5; ++lod)
    {
        unsigned int resolution = gridDimensions[lod];

        unsigned int vertexStart = (unsigned int)vertexCollection.size();
        unsigned int indexStart = (unsigned int)indexCollection.size();

        float invResolution = 1.0f / (float)resolution;

        for (unsigned int z = 0; z <= resolution; ++z)
        {
            float verticalCoord = z * invResolution;
            for (unsigned int x = 0; x <= resolution; ++x)
            {
                float horizontalCoord = x * invResolution;

                TerrainVertex vertexData{};
                vertexData.m_localPosition = XMFLOAT3(horizontalCoord - 0.5f, 0.0f, verticalCoord - 0.5f);
                vertexData.m_localNormal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                vertexData.m_textureCoordinate = XMFLOAT2(horizontalCoord, verticalCoord);

                vertexCollection.push_back(vertexData);
            }
        }

        unsigned int rowLength = resolution + 1;
        for (unsigned int z = 0; z < resolution; ++z)
        {
            for (unsigned int x = 0; x < resolution; ++x)
            {
                unsigned int idx0 = vertexStart + z * rowLength + x;
                unsigned int idx1 = idx0 + 1;
                unsigned int idx2 = vertexStart + (z + 1) * rowLength + x;
                unsigned int idx3 = idx2 + 1;

                indexCollection.push_back(idx0);
                indexCollection.push_back(idx2);
                indexCollection.push_back(idx1);

                indexCollection.push_back(idx1);
                indexCollection.push_back(idx2);
                indexCollection.push_back(idx3);
            }
        }

        SubmeshGeometry submeshInfo{};
        submeshInfo.IndexCount = resolution * resolution * 6;
        submeshInfo.StartIndexLocation = indexStart;
        submeshInfo.BaseVertexLocation = 0;

        m_meshData->DrawArgs[GetLODMeshIdentifier(lod)] = submeshInfo;
    }

    unsigned int vertexBytes = (unsigned int)vertexCollection.size() * sizeof(TerrainVertex);
    unsigned int indexBytes = (unsigned int)indexCollection.size() * sizeof(std::uint32_t);

    D3DCreateBlob(vertexBytes, &m_meshData->VertexBufferCPU);
    std::memcpy(m_meshData->VertexBufferCPU->GetBufferPointer(), vertexCollection.data(), vertexBytes);

    D3DCreateBlob(indexBytes, &m_meshData->IndexBufferCPU);
    std::memcpy(m_meshData->IndexBufferCPU->GetBufferPointer(), indexCollection.data(), indexBytes);

    m_meshData->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        graphicsDevice, commandList, vertexCollection.data(), vertexBytes, m_meshData->VertexBufferUploader);

    m_meshData->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        graphicsDevice, commandList, indexCollection.data(), indexBytes, m_meshData->IndexBufferUploader);

    m_meshData->VertexByteStride = sizeof(TerrainVertex);
    m_meshData->VertexBufferByteSize = vertexBytes;
    m_meshData->IndexFormat = DXGI_FORMAT_R32_UINT;
    m_meshData->IndexBufferByteSize = indexBytes;
}

float Terrain::GetHeight(float worldX, float worldZ) const
{
    if (m_heightfield.empty())
        return 0.0f;

    float u = (worldX / m_worldExtent + 0.5f) * (float)m_heightfieldWidth;
    float v = (worldZ / m_worldExtent + 0.5f) * (float)m_heightfieldHeight;

    int x0 = (int)std::floor(u);
    int z0 = (int)std::floor(v);

    float fracX = u - (float)x0;
    float fracZ = v - (float)z0;

    float h00 = FetchHeightSample(x0, z0);
    float h10 = FetchHeightSample(x0 + 1, z0);
    float h01 = FetchHeightSample(x0, z0 + 1);
    float h11 = FetchHeightSample(x0 + 1, z0 + 1);

    float hx0 = h00 + (h10 - h00) * fracX;
    float hx1 = h01 + (h11 - h01) * fracX;
    float interpolated = hx0 + (hx1 - hx0) * fracZ;

    return m_baseElevation + interpolated * (m_peakElevation - m_baseElevation);
}

XMFLOAT3 Terrain::GetNormal(float worldX, float worldZ) const
{
    float stepSize = m_worldExtent / (float)m_heightfieldWidth;

    float leftHeight = GetHeight(worldX - stepSize, worldZ);
    float rightHeight = GetHeight(worldX + stepSize, worldZ);
    float downHeight = GetHeight(worldX, worldZ - stepSize);
    float upHeight = GetHeight(worldX, worldZ + stepSize);

    XMFLOAT3 normalVector;
    normalVector.x = leftHeight - rightHeight;
    normalVector.y = 2.0f * stepSize;
    normalVector.z = downHeight - upHeight;

    XMVECTOR normalized = XMVector3Normalize(XMLoadFloat3(&normalVector));
    XMStoreFloat3(&normalVector, normalized);
    return normalVector;
}

float Terrain::FetchHeightSample(int xCoord, int zCoord) const
{
    int maxX = (int)m_heightfieldWidth - 1;
    int maxZ = (int)m_heightfieldHeight - 1;

    xCoord = (std::max)(0, (std::min)(xCoord, maxX));
    zCoord = (std::max)(0, (std::min)(zCoord, maxZ));

    return m_heightfield[(size_t)zCoord * (size_t)m_heightfieldWidth + (size_t)xCoord];
}

float Terrain::PerlinNoise(float xCoord, float zCoord) const
{
    int gridX = ((int)std::floor(xCoord)) & 255;
    int gridZ = ((int)std::floor(zCoord)) & 255;

    xCoord -= std::floor(xCoord);
    zCoord -= std::floor(zCoord);

    float u = Smoothstep(xCoord);
    float v = Smoothstep(zCoord);

    int a = m_permutationTable[gridX] + gridZ;
    int b = m_permutationTable[gridX + 1] + gridZ;

    float g00 = Gradient(m_permutationTable[a], xCoord, zCoord);
    float g10 = Gradient(m_permutationTable[b], xCoord - 1, zCoord);
    float g01 = Gradient(m_permutationTable[a + 1], xCoord, zCoord - 1);
    float g11 = Gradient(m_permutationTable[b + 1], xCoord - 1, zCoord - 1);

    float ix0 = Interpolate(g00, g10, u);
    float ix1 = Interpolate(g01, g11, u);

    return Interpolate(ix0, ix1, v);
}

float Terrain::Smoothstep(float t) const
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Terrain::Interpolate(float a, float b, float t) const
{
    return a + t * (b - a);
}

float Terrain::Gradient(int hashValue, float xComponent, float zComponent) const
{
    int h = hashValue & 3;
    float u = (h < 2) ? xComponent : zComponent;
    float v = (h < 2) ? zComponent : xComponent;

    float signA = (h & 1) ? -u : u;
    float signB = (h & 2) ? -v : v;
    return signA + signB;
}
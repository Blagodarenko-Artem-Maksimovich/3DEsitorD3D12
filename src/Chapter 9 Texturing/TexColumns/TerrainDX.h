#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <DirectXMath.h>
#include <string>

using Microsoft::WRL::ComPtr;

struct TerrainVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

class TerrainDX {
public:
    TerrainDX();
    ~TerrainDX();

    // device и cmdList должны быть действительны на момент Init (cmdList используется для копирования буферов)
    bool Init(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::wstring& heightmapPath,
        float horizontalScale = 1.0f, float heightScale = 20.0f, int gridSizeX = 256, int gridSizeZ = 256);

    void Render(ID3D12GraphicsCommandList* cmdList, const DirectX::XMMATRIX& worldViewProj);

    // освобождение ресурсов (если нужно вызвать вручную)
    void Destroy();

private:
    bool LoadHeightmap(const std::wstring& path);
    void BuildMesh(int cols, int rows, float hx, float hz, float heightScale);
    void ComputeNormals();

    bool CreateBuffersAndPSO(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

    // D3D resources
    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Resource> mIndexBuffer;
    ComPtr<ID3D12Resource> mVertexUpload;
    ComPtr<ID3D12Resource> mIndexUpload;
    D3D12_VERTEX_BUFFER_VIEW mVBView{};
    D3D12_INDEX_BUFFER_VIEW mIBView{};

    ComPtr<ID3D12RootSignature> mRootSig;
    ComPtr<ID3D12PipelineState> mPSO;

    // CPU-side mesh
    std::vector<TerrainVertex> mVertices;
    std::vector<uint32_t> mIndices;
    std::vector<uint8_t> mHeightData;
    int mHMWidth = 0;
    int mHMHeight = 0;

    // settings
    float mHorizontalScale = 1.0f;
    float mHeightScale = 20.0f;
    int mCols = 0, mRows = 0;

    // cached matrix slot (we'll upload via a small root CBV implemented as an upload buffer)
    ComPtr<ID3D12Resource> mPerObjectCB; // default: upload heap for simplicity in this sample
};

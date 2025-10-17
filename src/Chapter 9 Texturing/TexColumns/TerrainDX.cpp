#include "TerrainDX.h"
#include <d3dcompiler.h>
#include <cassert>
#include <fstream>
#include <algorithm>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace DirectX;

// Helpers
static inline UINT64 Align(UINT64 size, UINT64 align) {
    return (size + (align - 1)) & ~(align - 1);
}

static bool CompileShaderFromFile(const std::wstring& file, LPCSTR entry, LPCSTR target, ID3DBlob** blob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompileFromFile(file.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, flags, 0, blob, &err);
    if (FAILED(hr)) {
        if (err) {
            OutputDebugStringA((char*)err->GetBufferPointer());
            err->Release();
        }
        return false;
    }
    if (err) err->Release();
    return true;
}

// Copy CPU data to default heap using an upload heap and CopyBufferRegion
static HRESULT CreateDefaultBufferAndUpload(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData, UINT64 byteSize,
    ComPtr<ID3D12Resource>& defaultBuffer,
    ComPtr<ID3D12Resource>& uploadBuffer)
{
    if (!device || !cmdList || !initData) return E_INVALIDARG;

    // Default heap (GPU)
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&defaultBuffer));
    if (FAILED(hr)) return hr;

    // Upload heap (CPU-to-GPU)
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr)) return hr;

    // Copy data to upload buffer
    void* mapped = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    hr = uploadBuffer->Map(0, &readRange, &mapped);
    if (FAILED(hr)) return hr;
    memcpy(mapped, initData, (size_t)byteSize);
    uploadBuffer->Unmap(0, nullptr);

    // Schedule copy
    cmdList->CopyBufferRegion(defaultBuffer.Get(), 0, uploadBuffer.Get(), 0, byteSize);

    // Caller is responsible for transitioning defaultBuffer to VERTEX/INDEX state after copying
    return S_OK;
}

TerrainDX::TerrainDX() {}
TerrainDX::~TerrainDX() { Destroy(); }

void TerrainDX::Destroy() {
    mVertexBuffer.Reset();
    mIndexBuffer.Reset();
    mVertexUpload.Reset();
    mIndexUpload.Reset();
    mPerObjectCB.Reset();
    mPSO.Reset();
    mRootSig.Reset();
    mVertices.clear();
    mIndices.clear();
    mHeightData.clear();
}

bool TerrainDX::LoadHeightmap(const std::wstring& path) {
    // stb_image expects narrow path; convert
    std::string spath(path.begin(), path.end());
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(spath.c_str(), &w, &h, &ch, 1); // single channel
    if (!data) return false;
    mHMWidth = w; mHMHeight = h;
    mHeightData.assign(data, data + (w * h));
    stbi_image_free(data);
    return true;
}

void TerrainDX::BuildMesh(int cols, int rows, float hx, float hz, float heightScale) {
    mVertices.clear(); mIndices.clear();
    mCols = cols; mRows = rows;

    // Create (cols+1)*(rows+1) vertices
    for (int z = 0; z <= rows; ++z) {
        for (int x = 0; x <= cols; ++x) {
            float u = float(x) / float(cols);
            float v = float(z) / float(rows);

            // sample heightmap - simple nearest sampling
            int sx = std::min(mHMWidth - 1, std::max(0, int(u * (mHMWidth - 1))));
            int sz = std::min(mHMHeight - 1, std::max(0, int(v * (mHMHeight - 1))));
            uint8_t h = mHeightData[sz * mHMWidth + sx];
            float height = (h / 255.0f) * heightScale;

            TerrainVertex tv;
            tv.pos = XMFLOAT3((x - cols * 0.5f) * hx, height, (z - rows * 0.5f) * hz);
            tv.normal = XMFLOAT3(0, 1, 0); // будет пересчитано
            tv.uv = XMFLOAT2(u, v);
            mVertices.push_back(tv);
        }
    }

    // indices (two triangles per cell)
    for (int z = 0; z < rows; ++z) {
        for (int x = 0; x < cols; ++x) {
            uint32_t i0 = z * (cols + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + (cols + 1);
            uint32_t i3 = i2 + 1;
            // tri (i0, i2, i1) and (i1, i2, i3)
            mIndices.push_back(i0); mIndices.push_back(i2); mIndices.push_back(i1);
            mIndices.push_back(i1); mIndices.push_back(i2); mIndices.push_back(i3);
        }
    }

    ComputeNormals();
}

void TerrainDX::ComputeNormals() {
    // zero
    for (auto& v : mVertices) v.normal = XMFLOAT3(0, 0, 0);

    // accumulate face normals
    for (size_t i = 0; i + 2 < mIndices.size(); i += 3) {
        TerrainVertex& A = mVertices[mIndices[i + 0]];
        TerrainVertex& B = mVertices[mIndices[i + 1]];
        TerrainVertex& C = mVertices[mIndices[i + 2]];

        XMVECTOR a = XMLoadFloat3(&A.pos);
        XMVECTOR b = XMLoadFloat3(&B.pos);
        XMVECTOR c = XMLoadFloat3(&C.pos);

        XMVECTOR edge1 = b - a;
        XMVECTOR edge2 = c - a;
        XMVECTOR normal = XMVector3Cross(edge1, edge2);

        XMFLOAT3 nf;
        XMStoreFloat3(&nf, normal);

        A.normal.x += nf.x; A.normal.y += nf.y; A.normal.z += nf.z;
        B.normal.x += nf.x; B.normal.y += nf.y; B.normal.z += nf.z;
        C.normal.x += nf.x; C.normal.y += nf.y; C.normal.z += nf.z;
    }

    // normalize
    for (auto& v : mVertices) {
        XMVECTOR n = XMLoadFloat3(&v.normal);
        n = XMVector3Normalize(n);
        XMStoreFloat3(&v.normal, n);
    }
}

bool TerrainDX::CreateBuffersAndPSO(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    if (!device || !cmdList) return false;
    // Vertex buffer
    UINT64 vbByteSize = sizeof(TerrainVertex) * mVertices.size();
    HRESULT hr = CreateDefaultBufferAndUpload(device, cmdList, mVertices.data(), vbByteSize, mVertexBuffer, mVertexUpload);
    if (FAILED(hr)) return false;

    // Index buffer
    UINT64 ibByteSize = sizeof(uint32_t) * mIndices.size();
    hr = CreateDefaultBufferAndUpload(device, cmdList, mIndices.data(), ibByteSize, mIndexBuffer, mIndexUpload);
    if (FAILED(hr)) return false;

    // Barrier to transition default buffers from COPY_DEST to VERTEX/INDEX
    CD3DX12_RESOURCE_BARRIER vbBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CD3DX12_RESOURCE_BARRIER ibBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    cmdList->ResourceBarrier(1, &vbBarrier);
    cmdList->ResourceBarrier(1, &ibBarrier);

    // Fill VB/IB views
    mVBView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVBView.StrideInBytes = sizeof(TerrainVertex);
    mVBView.SizeInBytes = (UINT)vbByteSize;

    mIBView.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    mIBView.Format = DXGI_FORMAT_R32_UINT;
    mIBView.SizeInBytes = (UINT)ibByteSize;

    // Create a small upload CB for worldViewProj (we'll update it every frame simply by Map)
    {
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(Align(sizeof(XMMATRIX), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
        hr = device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mPerObjectCB));
        if (FAILED(hr)) return false;
    }

    // Create root signature (one CBV at b0)
    {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        param.Descriptor.ShaderRegister = 0;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 1;
        desc.pParameters = &param;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> error;
        HRESULT r = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
        if (FAILED(r)) {
            if (error) OutputDebugStringA((char*)error->GetBufferPointer());
            return false;
        }
        r = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&mRootSig));
        if (FAILED(r)) return false;
    }

    // Compile shaders
    ComPtr<ID3DBlob> vsBlob, psBlob;
    if (!CompileShaderFromFile(L"src/Common/TerrainVS.hlsl", "main", "vs_5_0", &vsBlob)) return false;
    if (!CompileShaderFromFile(L"src/Common/TerrainPS.hlsl", "main", "ps_5_0", &psBlob)) return false;

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(TerrainVertex, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(TerrainVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(TerrainVertex, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // PSO (very basic)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = mRootSig.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // single RTV, single DSV assumed to be created by app - choose common formats
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    HRESULT r = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO));
    if (FAILED(r)) return false;

    return true;
}

bool TerrainDX::Init(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::wstring& heightmapPath,
    float horizontalScale, float heightScale, int gridSizeX, int gridSizeZ)
{
    if (!device || !cmdList) return false;
    mHorizontalScale = horizontalScale; mHeightScale = heightScale;

    if (!LoadHeightmap(heightmapPath)) return false;

    BuildMesh(gridSizeX, gridSizeZ, horizontalScale, horizontalScale, heightScale);

    if (!CreateBuffersAndPSO(device, cmdList)) return false;

    return true;
}

void TerrainDX::Render(ID3D12GraphicsCommandList* cmdList, const XMMATRIX& worldViewProj) {
    if (!cmdList || !mPSO || !mRootSig) return;

    // Update per-object constant buffer (worldViewProj)
    if (mPerObjectCB) {
        // Map and write
        void* mapped = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        if (SUCCEEDED(mPerObjectCB->Map(0, &readRange, &mapped))) {
            XMMATRIX wvp = XMMatrixTranspose(worldViewProj);
            memcpy(mapped, &wvp, sizeof(XMMATRIX));
            mPerObjectCB->Unmap(0, nullptr);
        }
    }

    // Bind
    cmdList->SetGraphicsRootSignature(mRootSig.Get());
    // root parameter 0 -> CBV
    if (mPerObjectCB) {
        D3D12_GPU_VIRTUAL_ADDRESS addr = mPerObjectCB->GetGPUVirtualAddress();
        cmdList->SetGraphicsRootConstantBufferView(0, addr);
    }
    else {
        // nothing
    }

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mVBView);
    cmdList->IASetIndexBuffer(&mIBView);

    cmdList->SetPipelineState(mPSO.Get());
    cmdList->DrawIndexedInstanced((UINT)mIndices.size(), 1, 0, 0, 0);
}

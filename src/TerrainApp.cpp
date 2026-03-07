#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/GeometryGenerator.h"
#include "Common/Camera.h"
#include "Common/DDSTextureLoader.h"
#include "FrameResource.h"
#include "Terrain.h"
#include "QuadTree.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

const int g_concurrentFrames = 3;

struct TerrainBoundingBox
{
    XMFLOAT3 m_center;
    XMFLOAT3 m_halfDimensions;
};

class TerrainApp : public D3DApp
{
public:
    TerrainApp(HINSTANCE hInstance);
    ~TerrainApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void ProcessInput(const GameTimer& gt);
    void UpdateCameraOrientation(const GameTimer& gt);

    void PrepareObjectBuffers(const GameTimer& gt);
    void PreparePassBuffers(const GameTimer& gt);
    void PrepareTerrainBuffers(const GameTimer& gt);

    void CreateRootSignature();
    void CreateResourceViews();
    void CompileShaders();
    void CreatePipelineStates();
    void CreateFrameResources();

    void RenderTerrainPatches();
    bool RayTerrainIntersect(int mouseX, int mouseY, XMFLOAT3& hitPoint);
    void PaintOnTerrain(const XMFLOAT3& worldPos);
    void UpdatePaintTexture();

    void ComputeFrustumEdges(XMFLOAT4* pDest, const XMMATRIX& viewProj);
    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 2> GetStaticSamplerConfigs();

private:
    std::vector<std::unique_ptr<FrameResource>> m_frameResources;
    FrameResource* m_currentFrame = nullptr;
    int m_currentFrameIndex = 0;

    ComPtr<ID3D12RootSignature> m_rootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_srvDescriptorHeap = nullptr;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> m_shaderBytecode;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_pipelineObjects;
    std::vector<D3D12_INPUT_ELEMENT_DESC> m_vertexLayout;

    std::unique_ptr<Terrain> m_terrainSystem;
    TerrainBoundingBox m_terrainBounds = {};

    std::unique_ptr<QuadTree> m_spatialIndex;
    std::vector<TerrainNode*> m_renderableNodes;

    ComPtr<ID3D12Resource> m_heightfieldTexture;
    ComPtr<ID3D12Resource> m_heightfieldUpload;
    ComPtr<ID3D12Resource> m_diffuseTexture;
    ComPtr<ID3D12Resource> m_diffuseUpload;
    ComPtr<ID3D12Resource> m_normalTexture;
    ComPtr<ID3D12Resource> m_normalUpload;
    ComPtr<ID3D12Resource> m_fallbackWhite;
    ComPtr<ID3D12Resource> m_fallbackWhiteUpload;

    ComPtr<ID3D12Resource> m_paintTexture;
    ComPtr<ID3D12Resource> m_paintUploadBuffer;
    std::vector<UINT> m_paintData;

    PassConstants m_frameConstants = {};
    TerrainConstants m_terrainParameters = {};
    Camera m_viewCamera;

    XMFLOAT4 m_frustumPlanes[6] = {};

    bool m_terrainActive = true;
    bool m_wireframeEnabled = false;
    bool m_isPainting = false;
    bool m_paintTextureNeedsUpdate = false;
    float m_brushSize = 30.0f;
    XMFLOAT3 m_paintColor = { 1.0f, 0.0f, 0.0f };

    std::vector<float> m_lodThresholds = { 100.0f, 200.0f, 400.0f, 600.0f, 1000.0f };

    POINT m_lastCursorPosition = {};
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        TerrainApp application(hInstance);
        if (!application.Initialize())
            return 0;
        return application.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"Initialization Failure", MB_OK);
        return 0;
    }
}

TerrainApp::TerrainApp(HINSTANCE hInstance) : D3DApp(hInstance)
{
    mMainWndCaption = L"Terrain Painting System";
}

TerrainApp::~TerrainApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TerrainApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr);

    m_viewCamera.SetPosition(0.0f, 250.0f, 460.0f);
    m_viewCamera.LookAt(m_viewCamera.GetPosition3f(), XMFLOAT3(0, -30, 0), XMFLOAT3(0, 1, 0));

    m_terrainSystem = std::make_unique<Terrain>(md3dDevice.Get(), mCommandList.Get(), 512.0f, 0.0f, 150.0f);

    if (!m_terrainSystem->LoadHeightmapDDS(L"TerrainDetails/003/Height_Out.dds", md3dDevice.Get(), mCommandList.Get()))
    {
        m_terrainSystem->GenerateProceduralHeightmap(256, 256, 4.0f, 6);
    }

    m_terrainSystem->BuildGeometry(md3dDevice.Get(), mCommandList.Get());

    {
        const float halfSpan = m_terrainSystem->GetTerrainSize() * 0.5f;
        const float halfHeight = (m_terrainSystem->GetMaxHeight() - m_terrainSystem->GetMinHeight()) * 0.5f;

        m_terrainBounds.m_center = XMFLOAT3(0.0f, m_terrainSystem->GetMinHeight() + halfHeight, 0.0f);
        m_terrainBounds.m_halfDimensions = XMFLOAT3(halfSpan, halfHeight + 10.0f, halfSpan);
    }

    m_spatialIndex = std::make_unique<QuadTree>();
    const float minimumPatchSize = m_terrainSystem->GetTerrainSize() / 8.0f;

    m_spatialIndex->SetLODDistances(m_lodThresholds);
    m_spatialIndex->Initialize(m_terrainSystem->GetTerrainSize(), minimumPatchSize, 5);
    m_spatialIndex->SetHeightRange(0, 0, m_terrainSystem->GetTerrainSize(),
        m_terrainSystem->GetMinHeight(), m_terrainSystem->GetMaxHeight());

    CreateRootSignature();
    CreateResourceViews();
    CompileShaders();
    CreateFrameResources();
    CreatePipelineStates();

    mCommandList->Close();
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    FlushCommandQueue();

    return true;
}

void TerrainApp::OnResize()
{
    D3DApp::OnResize();
    m_viewCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 3000.0f);
}

void TerrainApp::Update(const GameTimer& gt)
{
    ProcessInput(gt);
    UpdateCameraOrientation(gt);

    m_currentFrameIndex = (m_currentFrameIndex + 1) % g_concurrentFrames;
    m_currentFrame = m_frameResources[m_currentFrameIndex].get();

    if (m_currentFrame->m_syncValue != 0 && mFence->GetCompletedValue() < m_currentFrame->m_syncValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        if (eventHandle)
        {
            mFence->SetEventOnCompletion(m_currentFrame->m_syncValue, eventHandle);
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }
    }

    {
        const XMMATRIX viewTransform = m_viewCamera.GetView();
        const XMMATRIX projTransform = m_viewCamera.GetProj();
        const XMMATRIX viewProjTransform = XMMatrixMultiply(viewTransform, projTransform);
        ComputeFrustumEdges(m_frustumPlanes, viewProjTransform);
    }

    {
        const XMFLOAT3 eyePosition = m_viewCamera.GetPosition3f();
        m_spatialIndex->Update(eyePosition, m_frustumPlanes);

        m_renderableNodes.clear();
        m_spatialIndex->GetVisibleNodes(m_renderableNodes);

        m_terrainActive = !m_renderableNodes.empty();
    }

    PrepareObjectBuffers(gt);
    PreparePassBuffers(gt);
    PrepareTerrainBuffers(gt);
}

void TerrainApp::Draw(const GameTimer& gt)
{
    auto allocator = m_currentFrame->m_allocator;
    allocator->Reset();

    mCommandList->Reset(allocator.Get(),
        m_wireframeEnabled ? m_pipelineObjects["terrain_wireframe"].Get() : m_pipelineObjects["terrain"].Get());

    if (m_paintTextureNeedsUpdate)
    {
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_paintTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

        D3D12_SUBRESOURCE_DATA paintData = {};
        paintData.pData = m_paintData.data();
        paintData.RowPitch = 512 * 4;
        paintData.SlicePitch = paintData.RowPitch * 512;

        UpdateSubresources(mCommandList.Get(), m_paintTexture.Get(), m_paintUploadBuffer.Get(), 0, 0, 1, &paintData);

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_paintTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        m_paintTextureNeedsUpdate = false;
    }

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* heaps[] = { m_srvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

    mCommandList->SetGraphicsRootSignature(m_rootSignature.Get());

    auto passResource = m_currentFrame->m_passConstants->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passResource->GetGPUVirtualAddress());

    auto terrainResource = m_currentFrame->m_terrainConstants->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, terrainResource->GetGPUVirtualAddress());

    CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(m_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootDescriptorTable(3, texHandle);

    if (m_terrainActive)
        RenderTerrainPatches();

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    mCommandList->Close();

    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(lists), lists);

    mSwapChain->Present(0, 0);
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    m_currentFrame->m_syncValue = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TerrainApp::RenderTerrainPatches()
{
    auto geometry = m_terrainSystem->GetGeometry();

    mCommandList->IASetVertexBuffers(0, 1, &geometry->VertexBufferView());
    mCommandList->IASetIndexBuffer(&geometry->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto objectBuffer = m_currentFrame->m_objectConstants->Resource();
    const unsigned int stride = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    for (size_t i = 0; i < m_renderableNodes.size(); ++i)
    {
        const TerrainNode* node = m_renderableNodes[i];

        const D3D12_GPU_VIRTUAL_ADDRESS address =
            objectBuffer->GetGPUVirtualAddress() + i * stride;

        mCommandList->SetGraphicsRootConstantBufferView(0, address);

        int lodIndex = (node->m_detailLevel < 4) ? node->m_detailLevel : 4;
        const char* meshKey = Terrain::GetLODMeshIdentifier(lodIndex);
        auto& submesh = geometry->DrawArgs[meshKey];

        mCommandList->DrawIndexedInstanced(
            submesh.IndexCount, 1,
            submesh.StartIndexLocation,
            submesh.BaseVertexLocation, 0);
    }
}

bool TerrainApp::RayTerrainIntersect(int mouseX, int mouseY, XMFLOAT3& hitPoint)
{
    float ndcX = (2.0f * mouseX) / mClientWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY) / mClientHeight;

    XMMATRIX view = m_viewCamera.GetView();
    XMMATRIX proj = m_viewCamera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
    XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
    XMVECTOR rayDir = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));

    XMFLOAT3 rayStart, rayDirection;
    XMStoreFloat3(&rayStart, nearPoint);
    XMStoreFloat3(&rayDirection, rayDir);

    float terrainSize = m_terrainSystem->GetTerrainSize();
    float halfSize = terrainSize * 0.5f;

    float lastT = 0.0f;
    for (float t = 1.0f; t < 3000.0f; t += 1.0f)
    {
        XMFLOAT3 testPoint = {
            rayStart.x + rayDirection.x * t,
            rayStart.y + rayDirection.y * t,
            rayStart.z + rayDirection.z * t
        };

        if (testPoint.x >= -halfSize && testPoint.x <= halfSize &&
            testPoint.z >= -halfSize && testPoint.z <= halfSize)
        {
            float terrainHeight = m_terrainSystem->GetHeight(testPoint.x, testPoint.z);

            if (testPoint.y <= terrainHeight)
            {
                float lo = lastT, hi = t;
                for (int i = 0; i < 16; ++i)
                {
                    float mid = (lo + hi) * 0.5f;
                    XMFLOAT3 midPoint = {
                        rayStart.x + rayDirection.x * mid,
                        rayStart.y + rayDirection.y * mid,
                        rayStart.z + rayDirection.z * mid
                    };
                    float midHeight = m_terrainSystem->GetHeight(midPoint.x, midPoint.z);
                    if (midPoint.y <= midHeight)
                        hi = mid;
                    else
                        lo = mid;
                }

                float finalT = (lo + hi) * 0.5f;
                hitPoint = {
                    rayStart.x + rayDirection.x * finalT,
                    m_terrainSystem->GetHeight(rayStart.x + rayDirection.x * finalT, rayStart.z + rayDirection.z * finalT),
                    rayStart.z + rayDirection.z * finalT
                };
                return true;
            }
        }
        lastT = t;
    }

    return false;
}

void TerrainApp::PaintOnTerrain(const XMFLOAT3& worldPos)
{
    float terrainSize = m_terrainSystem->GetTerrainSize();
    float halfSize = terrainSize * 0.5f;

    float u = (worldPos.x + halfSize) / terrainSize;
    float v = (worldPos.z + halfSize) / terrainSize;

    u = max(0.0f, min(1.0f, u));
    v = max(0.0f, min(1.0f, v));

    int paintWidth = 512;
    int paintHeight = 512;
    int centerX = (int)(u * (paintWidth - 1));
    int centerY = (int)(v * (paintHeight - 1));

    int brushRadius = max(2, (int)(m_brushSize * paintWidth / terrainSize));

    for (int y = centerY - brushRadius; y <= centerY + brushRadius; ++y)
    {
        for (int x = centerX - brushRadius; x <= centerX + brushRadius; ++x)
        {
            if (x >= 0 && x < paintWidth && y >= 0 && y < paintHeight)
            {
                float dx = (float)(x - centerX);
                float dy = (float)(y - centerY);
                float distance = sqrtf(dx * dx + dy * dy);

                if (distance <= (float)brushRadius)
                {
                    float alpha = 1.0f - (distance / (float)brushRadius);
                    alpha = alpha * alpha;

                    int index = y * paintWidth + x;
                    UINT& pixel = m_paintData[index];

                    float currentR = ((pixel >> 0) & 0xFF) / 255.0f;
                    float currentG = ((pixel >> 8) & 0xFF) / 255.0f;
                    float currentB = ((pixel >> 16) & 0xFF) / 255.0f;
                    float currentA = ((pixel >> 24) & 0xFF) / 255.0f;

                    float blendAlpha = alpha * 0.5f;
                    float newR = currentR * (1.0f - blendAlpha) + m_paintColor.x * blendAlpha;
                    float newG = currentG * (1.0f - blendAlpha) + m_paintColor.y * blendAlpha;
                    float newB = currentB * (1.0f - blendAlpha) + m_paintColor.z * blendAlpha;
                    float newA = min(currentA + blendAlpha, 1.0f);

                    UINT r = (UINT)(newR * 255.0f);
                    UINT g = (UINT)(newG * 255.0f);
                    UINT b = (UINT)(newB * 255.0f);
                    UINT a = (UINT)(newA * 255.0f);

                    pixel = (a << 24) | (b << 16) | (g << 8) | r;
                }
            }
        }
    }

    m_paintTextureNeedsUpdate = true;
}

void TerrainApp::UpdatePaintTexture()
{
    m_paintTextureNeedsUpdate = true;
}

void TerrainApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    m_lastCursorPosition.x = x;
    m_lastCursorPosition.y = y;

    if ((btnState & MK_LBUTTON) != 0)
    {
        m_isPainting = true;
        XMFLOAT3 hitPoint;
        if (RayTerrainIntersect(x, y, hitPoint))
        {
            PaintOnTerrain(hitPoint);
        }
    }

    SetCapture(mhMainWnd);
}

void TerrainApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    if (m_isPainting)
    {
        m_isPainting = false;
    }
    ReleaseCapture();
}

void TerrainApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0 && m_isPainting)
    {
        XMFLOAT3 hitPoint;
        if (RayTerrainIntersect(x, y, hitPoint))
        {
            PaintOnTerrain(hitPoint);
        }
    }

    if ((btnState & MK_RBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - m_lastCursorPosition.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - m_lastCursorPosition.y));
        m_viewCamera.Pitch(dy);
        m_viewCamera.RotateY(dx);
    }

    m_lastCursorPosition.x = x;
    m_lastCursorPosition.y = y;
}

void TerrainApp::ProcessInput(const GameTimer& gt)
{
    float delta = gt.DeltaTime();
    float baseSpeed = 100.0f;

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        baseSpeed *= 3.0f;

    if (GetAsyncKeyState('W') & 0x8000) m_viewCamera.Walk(baseSpeed * delta);
    if (GetAsyncKeyState('S') & 0x8000) m_viewCamera.Walk(-baseSpeed * delta);
    if (GetAsyncKeyState('A') & 0x8000) m_viewCamera.Strafe(-baseSpeed * delta);
    if (GetAsyncKeyState('D') & 0x8000) m_viewCamera.Strafe(baseSpeed * delta);

    if (GetAsyncKeyState('Q') & 0x8000)
    {
        XMFLOAT3 pos = m_viewCamera.GetPosition3f();
        m_viewCamera.SetPosition(pos.x, pos.y + baseSpeed * delta, pos.z);
    }

    if (GetAsyncKeyState('E') & 0x8000)
    {
        XMFLOAT3 pos = m_viewCamera.GetPosition3f();
        m_viewCamera.SetPosition(pos.x, pos.y - baseSpeed * delta, pos.z);
    }

    static bool latch = false;
    if (GetAsyncKeyState('1') & 0x8000)
    {
        if (!latch)
        {
            m_wireframeEnabled = !m_wireframeEnabled;
            latch = true;
        }
    }
    else
    {
        latch = false;
    }

    if (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000)
        m_brushSize = min(m_brushSize + 50.0f * delta, 100.0f);
    if (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000)
        m_brushSize = max(m_brushSize - 50.0f * delta, 5.0f);

    static bool rLatch = false, gLatch = false, bLatch = false;
    if (GetAsyncKeyState('R') & 0x8000)
    {
        if (!rLatch) { m_paintColor = { 1.0f, 0.0f, 0.0f }; rLatch = true; }
    }
    else { rLatch = false; }

    if (GetAsyncKeyState('G') & 0x8000)
    {
        if (!gLatch) { m_paintColor = { 0.0f, 1.0f, 0.0f }; gLatch = true; }
    }
    else { gLatch = false; }

    if (GetAsyncKeyState('B') & 0x8000)
    {
        if (!bLatch) { m_paintColor = { 0.0f, 0.0f, 1.0f }; bLatch = true; }
    }
    else { bLatch = false; }
}

void TerrainApp::UpdateCameraOrientation(const GameTimer& gt)
{
    m_viewCamera.UpdateViewMatrix();
}

void TerrainApp::PrepareObjectBuffers(const GameTimer& gt)
{
    auto objectBuffer = m_currentFrame->m_objectConstants.get();
    float extent = m_terrainSystem->GetTerrainSize();

    for (size_t i = 0; i < m_renderableNodes.size(); ++i)
    {
        const TerrainNode* node = m_renderableNodes[i];

        float patchScale = node->m_patchSize;
        float uvRange = node->m_patchSize / extent;
        float offsetU = (node->m_xCoordinate / extent) + 0.5f - uvRange * 0.5f;
        float offsetV = (node->m_zCoordinate / extent) + 0.5f - uvRange * 0.5f;

        XMMATRIX worldTransform =
            XMMatrixScaling(patchScale, 1.0f, patchScale) *
            XMMatrixTranslation(node->m_xCoordinate, 0.0f, node->m_zCoordinate);

        XMMATRIX uvTransform =
            XMMatrixScaling(uvRange, uvRange, 1.0f) *
            XMMatrixTranslation(offsetU, offsetV, 0.0f);

        ObjectConstants params{};
        XMStoreFloat4x4(&params.m_worldTransform, XMMatrixTranspose(worldTransform));
        XMStoreFloat4x4(&params.m_textureTransform, XMMatrixTranspose(uvTransform));
        params.m_materialSlot = 0;
        params.m_detailLevel = (node->m_detailLevel < 4) ? node->m_detailLevel : 4;

        objectBuffer->CopyData((int)i, params);
    }
}

void TerrainApp::PreparePassBuffers(const GameTimer& gt)
{
    XMMATRIX viewMatrix = m_viewCamera.GetView();
    XMMATRIX projMatrix = m_viewCamera.GetProj();
    XMMATRIX viewProjMatrix = XMMatrixMultiply(viewMatrix, projMatrix);

    XMMATRIX viewInv = XMMatrixInverse(&XMMatrixDeterminant(viewMatrix), viewMatrix);
    XMMATRIX projInv = XMMatrixInverse(&XMMatrixDeterminant(projMatrix), projMatrix);
    XMMATRIX viewProjInv = XMMatrixInverse(&XMMatrixDeterminant(viewProjMatrix), viewProjMatrix);

    XMStoreFloat4x4(&m_frameConstants.m_viewMatrix, XMMatrixTranspose(viewMatrix));
    XMStoreFloat4x4(&m_frameConstants.m_viewInverse, XMMatrixTranspose(viewInv));
    XMStoreFloat4x4(&m_frameConstants.m_projectionMatrix, XMMatrixTranspose(projMatrix));
    XMStoreFloat4x4(&m_frameConstants.m_projectionInverse, XMMatrixTranspose(projInv));
    XMStoreFloat4x4(&m_frameConstants.m_viewProjection, XMMatrixTranspose(viewProjMatrix));
    XMStoreFloat4x4(&m_frameConstants.m_viewProjectionInverse, XMMatrixTranspose(viewProjInv));

    m_frameConstants.m_cameraPosition = m_viewCamera.GetPosition3f();
    m_frameConstants.m_targetDimensions = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    m_frameConstants.m_targetDimensionsInv = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    m_frameConstants.m_planeNear = 1.0f;
    m_frameConstants.m_planeFar = 3000.0f;
    m_frameConstants.m_accumulatedTime = gt.TotalTime();
    m_frameConstants.m_frameDelta = gt.DeltaTime();
    m_frameConstants.m_ambientRadiance = { 0.3f, 0.3f, 0.35f, 1.0f };

    m_frameConstants.m_sceneLights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    m_frameConstants.m_sceneLights[0].Strength = { 0.9f, 0.85f, 0.8f };

    auto passBuffer = m_currentFrame->m_passConstants.get();
    passBuffer->CopyData(0, m_frameConstants);
}

void TerrainApp::PrepareTerrainBuffers(const GameTimer& gt)
{
    m_terrainParameters.m_elevationMinimum = m_terrainSystem->GetMinHeight();
    m_terrainParameters.m_elevationMaximum = m_terrainSystem->GetMaxHeight();
    m_terrainParameters.m_terrainExtent = m_terrainSystem->GetTerrainSize();
    m_terrainParameters.m_texelSpacing = 1.0f / m_terrainSystem->GetHeightmapWidth();
    m_terrainParameters.m_heightfieldResolution = XMFLOAT2(
        (float)m_terrainSystem->GetHeightmapWidth(),
        (float)m_terrainSystem->GetHeightmapHeight());

    auto terrainBuffer = m_currentFrame->m_terrainConstants.get();
    terrainBuffer->CopyData(0, m_terrainParameters);
}

void TerrainApp::ComputeFrustumEdges(XMFLOAT4* pDest, const XMMATRIX& viewProj)
{
    XMFLOAT4X4 M;
    XMStoreFloat4x4(&M, viewProj);

    pDest[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 };
    pDest[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 };
    pDest[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 };
    pDest[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 };
    pDest[4] = { M._13, M._23, M._33, M._43 };
    pDest[5] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR plane = XMLoadFloat4(&pDest[i]);
        plane = XMPlaneNormalize(plane);
        XMStoreFloat4(&pDest[i], plane);
    }
}

void TerrainApp::CreateRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[4];
    rootParameters[0].InitAsConstantBufferView(0);
    rootParameters[1].InitAsConstantBufferView(1);
    rootParameters[2].InitAsConstantBufferView(2);
    rootParameters[3].InitAsDescriptorTable(1, &textureRange, D3D12_SHADER_VISIBILITY_ALL);

    auto samplerConfigs = GetStaticSamplerConfigs();

    CD3DX12_ROOT_SIGNATURE_DESC signatureDescription(
        4, rootParameters,
        (UINT)samplerConfigs.size(), samplerConfigs.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized = nullptr;
    ComPtr<ID3DBlob> errorInfo = nullptr;
    D3D12SerializeRootSignature(&signatureDescription, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), errorInfo.GetAddressOf());

    md3dDevice->CreateRootSignature(0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(m_rootSignature.GetAddressOf()));
}

void TerrainApp::CreateResourceViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapConfig = {};
    heapConfig.NumDescriptors = 4;
    heapConfig.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapConfig.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    md3dDevice->CreateDescriptorHeap(&heapConfig, IID_PPV_ARGS(&m_srvDescriptorHeap));

    UINT paintWidth = 512;
    UINT paintHeight = 512;
    m_paintData.resize(paintWidth * paintHeight, 0x00000000);

    D3D12_RESOURCE_DESC paintTexDesc = {};
    paintTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    paintTexDesc.Width = paintWidth;
    paintTexDesc.Height = paintHeight;
    paintTexDesc.DepthOrArraySize = 1;
    paintTexDesc.MipLevels = 1;
    paintTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    paintTexDesc.SampleDesc.Count = 1;
    paintTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &paintTexDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_paintTexture));

    const UINT64 paintUploadSize = GetRequiredIntermediateSize(m_paintTexture.Get(), 0, 1);
    md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(paintUploadSize), D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&m_paintUploadBuffer));

    D3D12_SUBRESOURCE_DATA paintData = {};
    paintData.pData = m_paintData.data();
    paintData.RowPitch = paintWidth * 4;
    paintData.SlicePitch = paintData.RowPitch * paintHeight;

    UpdateSubresources(mCommandList.Get(), m_paintTexture.Get(), m_paintUploadBuffer.Get(), 0, 0, 1, &paintData);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_paintTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Height_Out.dds",
        m_heightfieldTexture, m_heightfieldUpload);

    if (FAILED(hr))
    {
        unsigned int width = m_terrainSystem->GetHeightmapWidth();
        unsigned int height = m_terrainSystem->GetHeightmapHeight();

        D3D12_RESOURCE_DESC textureSpec = {};
        textureSpec.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureSpec.Width = width;
        textureSpec.Height = height;
        textureSpec.DepthOrArraySize = 1;
        textureSpec.MipLevels = 1;
        textureSpec.Format = DXGI_FORMAT_R32_FLOAT;
        textureSpec.SampleDesc.Count = 1;
        textureSpec.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &textureSpec, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_heightfieldTexture));

        unsigned long long uploadSize = GetRequiredIntermediateSize(m_heightfieldTexture.Get(), 0, 1);
        md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadSize), D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&m_heightfieldUpload));

        std::vector<float> heightValues(width * height);
        for (unsigned int z = 0; z < height; ++z)
        {
            for (unsigned int x = 0; x < width; ++x)
            {
                float worldX = (float)x / width * m_terrainSystem->GetTerrainSize() - m_terrainSystem->GetTerrainSize() * 0.5f;
                float worldZ = (float)z / height * m_terrainSystem->GetTerrainSize() - m_terrainSystem->GetTerrainSize() * 0.5f;
                float elevation = m_terrainSystem->GetHeight(worldX, worldZ);
                heightValues[z * width + x] =
                    (elevation - m_terrainSystem->GetMinHeight()) / (m_terrainSystem->GetMaxHeight() - m_terrainSystem->GetMinHeight());
            }
        }

        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = heightValues.data();
        subData.RowPitch = width * sizeof(float);
        subData.SlicePitch = subData.RowPitch * height;

        UpdateSubresources(mCommandList.Get(), m_heightfieldTexture.Get(), m_heightfieldUpload.Get(),
            0, 0, 1, &subData);

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_heightfieldTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }

    DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Weathering_Out.dds",
        m_diffuseTexture, m_diffuseUpload);

    DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Normals_Out.dds",
        m_normalTexture, m_normalUpload);

    D3D12_RESOURCE_DESC fallbackSpec = {};
    fallbackSpec.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    fallbackSpec.Width = 1;
    fallbackSpec.Height = 1;
    fallbackSpec.DepthOrArraySize = 1;
    fallbackSpec.MipLevels = 1;
    fallbackSpec.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    fallbackSpec.SampleDesc.Count = 1;
    fallbackSpec.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &fallbackSpec, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_fallbackWhite));

    unsigned long long fallbackUploadSize = GetRequiredIntermediateSize(m_fallbackWhite.Get(), 0, 1);
    md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(fallbackUploadSize), D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&m_fallbackWhiteUpload));

    unsigned int whitePixel = 0xFFFFFFFF;
    D3D12_SUBRESOURCE_DATA whiteData = {};
    whiteData.pData = &whitePixel;
    whiteData.RowPitch = 4;
    whiteData.SlicePitch = 4;

    UpdateSubresources(mCommandList.Get(), m_fallbackWhite.Get(), m_fallbackWhiteUpload.Get(), 0, 0, 1, &whiteData);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_fallbackWhite.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srvSpec = {};
    srvSpec.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvSpec.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvSpec.Texture2D.MostDetailedMip = 0;

    {
        D3D12_RESOURCE_DESC hmDesc = m_heightfieldTexture->GetDesc();
        srvSpec.Format = hmDesc.Format;
        srvSpec.Texture2D.MipLevels = hmDesc.MipLevels;
        md3dDevice->CreateShaderResourceView(m_heightfieldTexture.Get(), &srvSpec, descriptorHandle);
        descriptorHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    {
        if (m_diffuseTexture)
        {
            D3D12_RESOURCE_DESC d = m_diffuseTexture->GetDesc();
            srvSpec.Format = d.Format;
            srvSpec.Texture2D.MipLevels = d.MipLevels;
            md3dDevice->CreateShaderResourceView(m_diffuseTexture.Get(), &srvSpec, descriptorHandle);
        }
        else
        {
            srvSpec.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvSpec.Texture2D.MipLevels = 1;
            md3dDevice->CreateShaderResourceView(m_fallbackWhite.Get(), &srvSpec, descriptorHandle);
        }
        descriptorHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    {
        if (m_normalTexture)
        {
            D3D12_RESOURCE_DESC n = m_normalTexture->GetDesc();
            srvSpec.Format = n.Format;
            srvSpec.Texture2D.MipLevels = n.MipLevels;
            md3dDevice->CreateShaderResourceView(m_normalTexture.Get(), &srvSpec, descriptorHandle);
        }
        else
        {
            srvSpec.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvSpec.Texture2D.MipLevels = 1;
            md3dDevice->CreateShaderResourceView(m_fallbackWhite.Get(), &srvSpec, descriptorHandle);
        }
        descriptorHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    {
        srvSpec.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvSpec.Texture2D.MipLevels = 1;
        md3dDevice->CreateShaderResourceView(m_paintTexture.Get(), &srvSpec, descriptorHandle);
    }
}

void TerrainApp::CompileShaders()
{
    m_shaderBytecode["terrainVS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "VS", "vs_5_1");
    m_shaderBytecode["terrainPS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS", "ps_5_1");
    m_shaderBytecode["terrainWirePS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS_Wireframe", "ps_5_1");

    m_vertexLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void TerrainApp::CreatePipelineStates()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDefinition = {};
    psoDefinition.InputLayout = { m_vertexLayout.data(), (UINT)m_vertexLayout.size() };
    psoDefinition.pRootSignature = m_rootSignature.Get();
    psoDefinition.VS = { m_shaderBytecode["terrainVS"]->GetBufferPointer(), m_shaderBytecode["terrainVS"]->GetBufferSize() };
    psoDefinition.PS = { m_shaderBytecode["terrainPS"]->GetBufferPointer(), m_shaderBytecode["terrainPS"]->GetBufferSize() };
    psoDefinition.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDefinition.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDefinition.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDefinition.SampleMask = UINT_MAX;
    psoDefinition.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDefinition.NumRenderTargets = 1;
    psoDefinition.RTVFormats[0] = mBackBufferFormat;
    psoDefinition.SampleDesc.Count = 1;
    psoDefinition.DSVFormat = mDepthStencilFormat;

    md3dDevice->CreateGraphicsPipelineState(&psoDefinition, IID_PPV_ARGS(&m_pipelineObjects["terrain"]));

    auto wireframeDefinition = psoDefinition;
    wireframeDefinition.PS = { m_shaderBytecode["terrainWirePS"]->GetBufferPointer(), m_shaderBytecode["terrainWirePS"]->GetBufferSize() };
    wireframeDefinition.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

    md3dDevice->CreateGraphicsPipelineState(&wireframeDefinition, IID_PPV_ARGS(&m_pipelineObjects["terrain_wireframe"]));
}

void TerrainApp::CreateFrameResources()
{
    const unsigned int maxObjectSlots = 256;

    for (int i = 0; i < g_concurrentFrames; ++i)
        m_frameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, maxObjectSlots, 1));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 2> TerrainApp::GetStaticSamplerConfigs()
{
    const CD3DX12_STATIC_SAMPLER_DESC wrapLinear(
        0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC clampLinear(
        1, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    return { wrapLinear, clampLinear };
}
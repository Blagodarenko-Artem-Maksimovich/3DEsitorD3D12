#pragma once
#include "Camera.h"
#include "d3dApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include <filesystem>
#include "FrameResource.h"
#include <iostream>
#include "RenderItem.h"
#include "TerrainSystem.h"
#include "NoiseGeneration.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

class GraphicsLabApp : public D3DApp
{
public:
    GraphicsLabApp(HINSTANCE hInstance);
    GraphicsLabApp(const GraphicsLabApp& rhs) = delete;
    GraphicsLabApp& operator=(const GraphicsLabApp& rhs) = delete;
    ~GraphicsLabApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& timer) override;
    virtual void DeferredDraw(const GameTimer& timer) override;
    virtual void OnMouseDown(WPARAM buttonState, int x, int y) override;
    virtual void OnMouseUp(WPARAM buttonState, int x, int y) override;
    virtual void OnMouseMove(WPARAM buttonState, int x, int y) override;
    virtual void MoveBackFwd(float amount) override;
    virtual void MoveLeftRight(float amount) override;
    virtual void MoveUpDown(float amount) override;
    void InitializeImGui();
    void OnKeyPressed(const GameTimer& timer, WPARAM keyCode) override;
    void OnKeyReleased(const GameTimer& timer, WPARAM keyCode) override;
    std::wstring GetCameraSpeed() override;

    void ProcessCamera(const GameTimer& timer);
    void InitializeShadowResources();
    void ProcessMaterialAnimations(const GameTimer& timer);
    void RefreshObjectConstantBuffers(const GameTimer& timer);
    void RefreshTerrainConstantBuffers(const GameTimer& timer);
    void RefreshLightConstantBuffers(const GameTimer& timer);
    void RefreshMaterialConstantBuffers(const GameTimer& timer);
    void RefreshMainPassConstantBuffer(const GameTimer& timer);
    void CreateGBuffers() override;
    void InitializeTextureResources();
    void LoadTextureResource(const std::string& textureName);
    void SetupMainRootSignature();
    void SetupTerrainRootSignature();
    void SetupLightingRootSignature();
    void SetupShadowRootSignature();
    void InitializeLightSources();
    void ConfigureLightGeometry();
    void CreateDescriptorHeaps();
    void CompileShadersAndSetupLayout();
    void CreateGeometryResources();
    void CreatePipelineStates();
    void InitializeFrameData();
    void CreateMaterialResource(std::string materialName, int constantBufferSlot, int diffuseSlot, int normalSlot, XMFLOAT4 albedo, XMFLOAT3 fresnel, float roughness);
    void InitializeMaterials();
    void CreateMeshInstance(std::string instanceId, std::string meshId, std::string materialId, XMFLOAT3 scale, XMFLOAT3 rotation, XMFLOAT3 position);
    void ProcessMeshGeometry(std::string meshId, UINT& vertexStart, UINT& indexStart, UINT& previousVertexCount, UINT& previousIndexCount, std::vector<Vertex>& vertexData, std::vector<std::uint16_t>& indexData, MeshGeometry* geometry);
    void CreateRenderableObjects();
    void RenderShadowMap();
    void RenderObjects(ID3D12GraphicsCommandList* commandList, const std::vector<RenderItem*>& objects);
    void RenderTerrainTiles(ID3D12GraphicsCommandList* commandList, std::vector<TerrainTile*> tiles, int heightMapIndex);
    void UpdateImGuiInterface();
    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> CreateStaticSamplers();
    void AddSpotLight(XMFLOAT3 position, XMFLOAT3 direction, XMFLOAT3 color, float innerAngle, float outerAngle, float intensity, float focus);
    void AddPointLight(XMFLOAT3 position, XMFLOAT3 color, float startAttenuation, float endAttenuation, float intensity);

private:
    std::unordered_map<std::string, unsigned int> m_objectCounts;
    std::vector<std::unique_ptr<FrameResource>> m_frameData;
    FrameResource* m_currentFrame = nullptr;
    int m_currentFrameIndex = 0;

    std::unordered_map<std::string, int> m_textureIndices;
    UINT m_descriptorSize = 0;

    ComPtr<ID3D12RootSignature> m_mainRootSignature = nullptr;
    ComPtr<ID3D12RootSignature> m_terrainRootSignature = nullptr;
    ComPtr<ID3D12RootSignature> m_lightRootSignature = nullptr;
    ComPtr<ID3D12RootSignature> m_shadowRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> m_mainSrvHeap = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_imGuiSrvHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> m_geometryCache;
    std::unordered_map<std::string, std::unique_ptr<Material>> m_materialCache;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textureCache;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> m_shaderCache;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_pipelineStates;

    std::vector<D3D12_INPUT_ELEMENT_DESC> m_vertexLayout;

    std::vector<std::unique_ptr<RenderItem>> m_renderQueue;
    std::vector<Light> m_lightList;
    std::vector<RenderItem*> m_opaqueObjects;

    PassConstants m_passConstants;
    XMFLOAT3 m_cameraPosition = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 m_viewMatrix = MathHelper::Identity4x4();
    XMFLOAT4X4 m_projectionMatrix = MathHelper::Identity4x4();

    float m_verticalAngle = 1.5f * XM_PI;
    float m_horizontalAngle = 0.2f * XM_PI;
    float m_cameraDistance = 15.0f;

    POINT m_previousMousePosition;

    ComPtr<ID3D12Resource> m_positionBuffer;
    ComPtr<ID3D12Resource> m_normalBuffer;
    ComPtr<ID3D12Resource> m_albedoBuffer;
    ComPtr<ID3D12Resource> m_depthStencilBuffer;
    ComPtr<ID3D12DescriptorHeap> m_gBufferSrvHeap = nullptr;

    CD3DX12_CPU_DESCRIPTOR_HANDLE m_gBufferRenderTargets[3];
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_gBufferDepthView;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_gBufferShaderViews[3];

    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;

    const UINT SHADOW_RESOLUTION = 2048;
    const DXGI_FORMAT SHADOW_RESOURCE_FORMAT = DXGI_FORMAT_R24G8_TYPELESS;
    const DXGI_FORMAT SHADOW_DEPTH_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
    const DXGI_FORMAT SHADOW_VIEW_FORMAT = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowResource;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shadowDepthHeap;
    D3D12_VIEWPORT m_shadowViewport;
    D3D12_RECT m_shadowScissorRect;

    UINT m_renderWidth = mClientWidth;
    UINT m_renderHeight = mClientHeight;

    const DXGI_FORMAT m_positionBufferFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
    const DXGI_FORMAT m_normalBufferFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    const DXGI_FORMAT m_albedoBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    std::unique_ptr<TerrainSystem> m_terrainManager;
    XMFLOAT4 m_viewFrustum[6];
    std::vector<TerrainTile*> m_visibleTiles;
    float m_terrainHeightScale = 100;
    ComPtr<ID3D12Resource> m_heightMapResource;
    NoiseGenerator m_heightNoiseGenerator;
    Camera m_viewCamera;

    void GenerateTerrainMesh(const XMFLOAT3& center, float size, int detail, std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices);
    void BuildTerrainMeshes();
    void UpdateTerrainSystem(const GameTimer& timer);
    void RegenerateTerrainHeight();
};
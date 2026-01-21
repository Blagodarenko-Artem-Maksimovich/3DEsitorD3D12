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


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;


// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.


class CGLAB : public D3DApp
{
public:
	CGLAB(HINSTANCE hInstance);
	CGLAB(const CGLAB& rhs) = delete;
	CGLAB& operator=(const CGLAB& rhs) = delete;
	~CGLAB();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void DeferredDraw(const GameTimer& gt)override;
	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
	virtual void MoveBackFwd(float step)override;
	virtual void MoveLeftRight(float step)override;
	virtual void MoveUpDown(float step)override;
	void ImguiInit();
	void OnKeyPressed(const GameTimer& gt, WPARAM key) override;
	void OnKeyReleased(const GameTimer& gt, WPARAM key) override;
	std::wstring GetCamSpeed() override;
	void UpdateCamera(const GameTimer& gt);
	void BuildShadowMapViews();
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateTerrainCBs(const GameTimer& gt);
	void UpdateLightCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void CreateGBuffer() override;
	void LoadAllTextures();
	void LoadTexture(const std::string& name);
	void BuildRootSignature();
	void BuildTerrainRootSignature();
	void BuildLightingRootSignature();
	void BuildShadowPassRootSignature();
	void BuildLights();
	void SetLightShapes();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void CreateMaterial(std::string _name, int _CBIndex, int _SRVDiffIndex, int _SRVNMapIndex, XMFLOAT4 _DiffuseAlbedo, XMFLOAT3 _FresnelR0, float _Roughness);
	void BuildMaterials();
	void RenderCustomMesh(std::string unique_name, std::string meshname, std::string materialName, XMFLOAT3 Scale, XMFLOAT3 Rotation, XMFLOAT3 Position);
	void BuildCustomMeshGeometry(std::string name, UINT& meshVertexOffset, UINT& meshIndexOffset, UINT& prevVertSize, UINT& prevIndSize, std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, MeshGeometry* Geo);
	void BuildRenderItems();
	void DrawSceneToShadowMap();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawTilesRenderItems(ID3D12GraphicsCommandList* cmdList, std::vector<TerrainTile*> tiles, int HeightIndex);
	void RenderIMGUI();
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
	void CreateSpotLight(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 color, float faloff_start, float faloff_end, float strength, float spotpower);
	void CreatePointLight(XMFLOAT3 pos, XMFLOAT3 color, float faloff_start, float faloff_end, float strength);

private:
	std::unordered_map<std::string, unsigned int>ObjectsMeshCount;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;
	//
	std::unordered_map<std::string, int>TexOffsets;
	//
	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mTerrainRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mLightingRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mShadowPassRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> m_ImGuiSrvDescriptorHeap; // Member variable

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<Light>mLights;
	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;
	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;

	// G-Buffer ресурсы
	ComPtr<ID3D12Resource> mGBufferPosition;
	ComPtr<ID3D12Resource> mGBufferNormal;
	ComPtr<ID3D12Resource> mGBufferAlbedo;
	ComPtr<ID3D12Resource> mGBufferDepthStencil;
	ComPtr<ID3D12DescriptorHeap> mGBufferSrvHeap = nullptr;

	// Дескрипторы для G-Buffer
	CD3DX12_CPU_DESCRIPTOR_HANDLE mGBufferRTVs[3]; // 0:Position, 1:Normal, 2:Albedo
	CD3DX12_CPU_DESCRIPTOR_HANDLE mGBufferDSV;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mGBufferSRVs[3]; // SRV для шейдеров

	UINT mGBufferRTVDescriptorSize;
	UINT mGBufferDSVDescriptorSize;

	// shadow resources 
	const UINT SHADOW_MAP_WIDTH = 2048;
	const UINT SHADOW_MAP_HEIGHT = 2048;
	const DXGI_FORMAT SHADOW_MAP_FORMAT = DXGI_FORMAT_R24G8_TYPELESS; // Resource format
	const DXGI_FORMAT SHADOW_MAP_DSV_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT; // DSV format
	const DXGI_FORMAT SHADOW_MAP_SRV_FORMAT = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; // SRV format
	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap; // A separate heap for shadow map DSVs
	D3D12_VIEWPORT mShadowViewport;
	D3D12_RECT mShadowScissorRect;

	// Размеры как у окна
	UINT width = mClientWidth;
	UINT height = mClientHeight;

	// Форматы:
	const DXGI_FORMAT positionFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	const DXGI_FORMAT normalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	const DXGI_FORMAT albedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;


	// Terrain system
	std::unique_ptr<TerrainSystem> m_terrainSystem;
	XMFLOAT4 m_frustumPlanes[6];  // Плоскости frustum'a
	std::vector<TerrainTile*> m_visibleTerrainTiles;
	float heightScale = 100;
	// Методы
	void GenerateTileGeometry(const XMFLOAT3& worldPos, float tileSize, int lodLevel, std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices);
	void BuildTerrainGeometry();
	void UpdateTerrain(const GameTimer& gt);

	Camera cam;
};
//***************************************************************************************
// TexColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************
#include "../../Common/d3dUtil.h"
#include "../../Common/Camera.h"
#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include <filesystem>
#include "FrameResource.h"
#include "Terrain.h"
#include <iostream>
#include <algorithm> 
#include <cmath>
#include <cctype>
#include <dxcapi.h>


#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui.h"
Camera cam;
static int imguiID = 0;
static bool CCenabled = false;
static bool enableMovement = false;
// 0 = Off, 1 = Moving objects (CPU/world delta), 2 = Velocity buffer (camera+objects), 3 = Velocity buffer object-only (approx)
static int gVelocityDebugMode = 0;
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
// Note: we load DXC (dxcompiler.dll) dynamically at runtime for DXR shaders.

static ComPtr<ID3DBlob> CompileShaderDXC(
	const std::wstring& filename,
	const std::wstring& entrypoint,
	const std::wstring& target,
	const std::vector<LPCWSTR>& extraArgs = {})
{
	// Dynamically load DXC so the app can still run without it (falls back to shadow maps).
	using DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
	static HMODULE sDxcompiler = nullptr;
	static DxcCreateInstanceProc sDxcCreateInstance = nullptr;

	if (!sDxcCreateInstance)
	{
		sDxcompiler = LoadLibraryW(L"dxcompiler.dll");
		if (!sDxcompiler)
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

		sDxcCreateInstance = (DxcCreateInstanceProc)GetProcAddress(sDxcompiler, "DxcCreateInstance");
		if (!sDxcCreateInstance)
			ThrowIfFailed(E_FAIL);
	}

	// Avoid linking against CLSID_* globals (which would reintroduce dxcompiler.lib dependency).
	static const CLSID CLSID_DxcUtilsLocal =
	{ 0x6245d6af, 0x66e0, 0x48fd, { 0x80, 0xb4, 0x4d, 0x27, 0x17, 0x96, 0x74, 0x8c } };
	static const CLSID CLSID_DxcCompilerLocal =
	{ 0x73e22d93, 0xe6ce, 0x47f3, { 0xb5, 0xbf, 0xf0, 0x66, 0x4f, 0x39, 0xc1, 0xb0 } };

	ComPtr<IDxcUtils> utils;
	ComPtr<IDxcCompiler3> compiler;
	ThrowIfFailed(sDxcCreateInstance(CLSID_DxcUtilsLocal, IID_PPV_ARGS(&utils)));
	ThrowIfFailed(sDxcCreateInstance(CLSID_DxcCompilerLocal, IID_PPV_ARGS(&compiler)));

	ComPtr<IDxcIncludeHandler> includeHandler;
	ThrowIfFailed(utils->CreateDefaultIncludeHandler(&includeHandler));

	uint32_t codePage = DXC_CP_UTF8;
	ComPtr<IDxcBlobEncoding> source;
	ThrowIfFailed(utils->LoadFile(filename.c_str(), &codePage, &source));

	DxcBuffer srcBuf = {};
	srcBuf.Ptr = source->GetBufferPointer();
	srcBuf.Size = source->GetBufferSize();
	srcBuf.Encoding = DXC_CP_UTF8;

	std::vector<LPCWSTR> args;
	args.reserve(16 + extraArgs.size());
	args.push_back(filename.c_str());
	args.push_back(L"-E"); args.push_back(entrypoint.c_str());
	args.push_back(L"-T"); args.push_back(target.c_str());
#if defined(DEBUG) || defined(_DEBUG)
	args.push_back(L"-Zi");
	args.push_back(L"-Qembed_debug");
	args.push_back(L"-Od");
#else
	args.push_back(L"-O3");
#endif
	// HLSL uses column-major by default; keep consistent with existing shaders.
	// Add shader folder for includes.
	args.push_back(L"-I"); args.push_back(L"Shaders");

	for (auto a : extraArgs) args.push_back(a);

	ComPtr<IDxcResult> result;
	ThrowIfFailed(compiler->Compile(&srcBuf, args.data(), (uint32_t)args.size(),
		includeHandler.Get(), IID_PPV_ARGS(&result)));

	ComPtr<IDxcBlobUtf8> errors;
	ThrowIfFailed(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
	if (errors && errors->GetStringLength() > 0)
		OutputDebugStringA(errors->GetStringPointer());

	HRESULT status = S_OK;
	ThrowIfFailed(result->GetStatus(&status));
	ThrowIfFailed(status);

	ComPtr<IDxcBlob> dxil;
	ThrowIfFailed(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxil), nullptr));

	ComPtr<ID3DBlob> byteCode;
	ThrowIfFailed(D3DCreateBlob(dxil->GetBufferSize(), byteCode.GetAddressOf()));
	memcpy(byteCode->GetBufferPointer(), dxil->GetBufferPointer(), dxil->GetBufferSize());
	return byteCode;
}

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 PrevWorld = MathHelper::Identity4x4(); // NEW
	XMMATRIX ScaleM = XMMatrixIdentity();
	XMMATRIX RotationM = XMMatrixIdentity();
	XMMATRIX TranslationM = XMMatrixIdentity();
	XMFLOAT3 Position = { 0.0f, 0.0f, 2.0f };
	XMFLOAT3 RotationAngle = { 0.0f, .0f, 0.0f };
	XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	Material* BaseMat = nullptr; // Original material (restore after debug overrides)
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
	std::string Name;


};

struct TAAConstants
{
	float Alpha = 0.1f;
	float ClampExpand = 0.0f;
	DirectX::XMFLOAT2 InvRTSize = { 0,0 };
	float TaaStrength = 1.0f;
	float _pad[3] = { 0,0,0 };
};
static_assert(sizeof(TAAConstants) == 32);



struct TAAReprojectConstants
{
	DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 PrevViewProj = MathHelper::Identity4x4();
};

class TexColumnsApp : public D3DApp
{
public:
	TexColumnsApp(HINSTANCE hInstance);
	TexColumnsApp(const TexColumnsApp& rhs) = delete;
	TexColumnsApp& operator=(const TexColumnsApp& rhs) = delete;
	~TexColumnsApp();

	virtual bool Initialize()override;


private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;
	virtual void DeferredDraw(const GameTimer& gt)override;
	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
	virtual void MoveBackFwd(float step)override;
	virtual void MoveLeftRight(float step)override;
	virtual void MoveUpDown(float step)override;
	void OnKeyPressed(const GameTimer& gt, WPARAM key) override;
	void OnKeyReleased(const GameTimer& gt, WPARAM key) override;
	std::wstring GetCamSpeed() override;
	void UpdateCamera(const GameTimer& gt);
	void BuildShadowMapViews();
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateLightCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void CreateGBuffer() override;
	void CreateSceneTexture();
	void LoadAllTextures();
	void LoadTexture(const std::string& name);
	void BuildRootSignature();
	void BuildLightingRootSignature();
	void BuildShadowPassRootSignature();
	void BuildPostProcessRootSignature();
	void BuildLights();
	void SetLightShapes();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void RotateSpotlightTowardCursor(int x, int y);
	void CreateMaterial(std::string _name, int _CBIndex, int _SRVDiffIndex, int _SRVNMapIndex, XMFLOAT4 _DiffuseAlbedo, XMFLOAT3 _FresnelR0, float _Roughness, float _Metallic);
	void BuildMaterials();
	void RenderCustomMesh(std::string unique_name, std::string meshname, std::string materialName, XMFLOAT3 Scale, XMFLOAT3 Rotation, XMFLOAT3 Position);
	void BuildCustomMeshGeometry(std::string name, UINT& meshVertexOffset, UINT& meshIndexOffset, UINT& prevVertSize, UINT& prevIndSize, std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, MeshGeometry* Geo);
	void BuildRenderItems();
	void DrawSceneToShadowMap();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
	void CreateTaaHistoryTextures();
	void CreateTaaHistoryRtvs();
	void BuildTaaRootSignature();
	void CreateTaaDepthHistoryTextures();
	void Transition(ID3D12Resource* res, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES newState);
	void CreateSpotLight(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 color, float faloff_start, float faloff_end, float strength, float spotpower);
	void CreatePointLight(XMFLOAT3 pos, XMFLOAT3 color, float faloff_start, float faloff_end, float strength);

	void LoadTerrainTextures();
	void BuildTerrainGeometry();
	void DrawTerrain(ID3D12GraphicsCommandList* cmdList);
	void BuildDxrShadowRootSignature();
	void BuildDxrShadowPSO();
	void BuildDxrAccelerationStructures();
	void CreateDxrShadowMaskResources();
	void CreateDxrShadowDescriptors();
	void DispatchDxrShadowMask(ID3D12GraphicsCommandList* cmdList);

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
	ComPtr<ID3D12RootSignature> mLightingRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mShadowPassRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> m_ImGuiSrvDescriptorHeap; // Member variable
	bool mImGuiInitialized = false;

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
	ComPtr<ID3D12Resource> mGBufferVelocity;


	// Дескрипторы для G-Buffer
	CD3DX12_CPU_DESCRIPTOR_HANDLE mGBufferRTVs[4]; // 0:Position, 1:Normal, 2:Albedo
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

	// post-process resources
	ComPtr<ID3D12Resource> mSceneTexture;        // Texture to hold the lit scene
	CD3DX12_CPU_DESCRIPTOR_HANDLE mSceneRtvHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mSceneSrvHandle; // GPU handle for the SRV
	UINT mSceneSrvHeapIndex = -1; // Index in your main SRV heap if you combine them

	ComPtr<ID3D12RootSignature> mPostProcessRootSignature = nullptr;
	std::unique_ptr<UploadBuffer<float>> mChromaticAberrationCB = nullptr; // Constant buffer for offset
	std::unique_ptr<UploadBuffer<TAAConstants>> mTaaCB = nullptr;


	float gChromaticAberrationOffset = 0.000f;

	XMFLOAT4X4 mBaseProj = MathHelper::Identity4x4(); // projection без джиттера
	UINT mJitterIndex = 0;
	static const UINT kJitterCount = 8;
	XMFLOAT2 mJitter = XMFLOAT2(0.0f, 0.0f); 

	ComPtr<ID3D12Resource> mTaaHistory[2];
	CD3DX12_CPU_DESCRIPTOR_HANDLE mTaaHistoryRtv[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mTaaHistorySrv[2];
	UINT mTaaHistorySrvIndex[2] = { 0, 0 };
	UINT mTaaHistoryIndex = 0; // read index

	Microsoft::WRL::ComPtr<ID3D12RootSignature> mTaaRootSignature;

	DirectX::XMFLOAT4X4 mPrevViewProj = MathHelper::Identity4x4();
	bool mTaaHistoryValid = false;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mDepthSrvHandle{};
	UINT mDepthSrvHeapIndex = 0;
	std::unique_ptr<UploadBuffer<TAAReprojectConstants>> mTaaReprojectCB = nullptr;

	std::unique_ptr<UploadBuffer<AtmosphereConstants>> mAtmosphereCB = nullptr;
	AtmosphereConstants mAtmosphereParams;

	std::unique_ptr<Terrain> mTerrain;
	std::vector<int> mTerrainHeightmapIndicesLOD0;
	std::vector<int> mTerrainHeightmapIndicesLOD1;
	std::vector<int> mTerrainHeightmapIndicesLOD2;
	int mTerrainMaterialIndex = -1;
	float mTerrainHeightScale = 50.0f;
	float mTerrainWorldSize = 100.0f;
	float mTerrainLOD1Factor = 0.6f;
	float mTerrainLOD2Factor = 0.3f;
	int mTerrainFallbackHeightmapIndex = -1;
	bool mTerrainEnabled = true;
	bool mTerrainWireframe = false;
	float mTerrainOriginY = 125.0f;  // above PBR spheres (y=120)

	// ---------------- DXR (RayQuery) shadows ----------------
	bool mEnableDxrShadows = true;
	bool mVisualizeDxrShadowMask = false;
	UINT mDxrShadowSamples = 1;           // safe default (TAA accumulates)
	int mDxrShadowDownscale = 2;          // 1=full,2=half,4=quarter
	int mPrevDxrShadowDownscale = 2;
	bool mDxrUpdateTlasEveryFrame = false;
	float mDxrConeAngleDeg = 1.0f;        // cone half-angle (deg), 0 = hard
	float mDxrMaxDistance = 2000.0f;
	float mDxrNormalBias = 0.02f;
	UINT mDxrFrameIndex = 0;
	int mDxrShadowLightCBIndex = 0; // which LightCBIndex uses DXR mask (directional)

	ComPtr<ID3D12RootSignature> mDxrShadowRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mDxrShadowPSO = nullptr;
	ComPtr<ID3DBlob> mDxrShadowCS = nullptr;

	ComPtr<ID3D12Resource> mDxrShadowMask = nullptr; // R16_FLOAT UAV/SRV
	D3D12_RESOURCE_STATES mDxrShadowMaskState = D3D12_RESOURCE_STATE_COMMON;

	// Acceleration structures
	ComPtr<ID3D12Resource> mDxrBlasScratch = nullptr;
	ComPtr<ID3D12Resource> mDxrTlasScratch = nullptr;
	ComPtr<ID3D12Resource> mDxrTlas = nullptr;
	std::vector<ComPtr<ID3D12Resource>> mDxrInstanceDescs; // per-frame instance desc buffers (upload)
	std::vector<ComPtr<ID3D12Resource>> mDxrBlas; // cached BLAS per unique submesh
	struct DxrInstanceRef
	{
		RenderItem* Ri = nullptr;
		UINT BlasIndex = 0;
	};
	std::vector<DxrInstanceRef> mDxrInstances; // TLAS instance list (stable order)

	// Descriptor heap indices (allocated in BuildDescriptorHeaps)
	int mDxrTlasSrvIndex = -1;
	int mDxrShadowMaskUavIndex = -1;
	int mDxrShadowMaskSrvIndex = -1;

	struct DxrShadowConstants
	{
		DirectX::XMFLOAT3 LightDirW = { 0.0f, -1.0f, 0.0f };
		float ConeAngleRad = 0.0f;
		UINT SampleCount = 1;
		UINT FrameIndex = 0;
		UINT Downscale = 1;
		UINT _pad0 = 0;
		float MaxDistance = 2000.0f;
		float NormalBias = 0.02f;
		DirectX::XMFLOAT2 _pad1 = { 0.0f, 0.0f };
	};
	std::unique_ptr<UploadBuffer<DxrShadowConstants>> mDxrShadowCB = nullptr;

	bool mDayNightCycleEnabled = false;
	float mDayNightCycleDuration = 7.0f; // day timing

	Microsoft::WRL::ComPtr<ID3D12Resource> mTaaDepthHistory[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mTaaDepthHistorySrv[2];

	float mTaaAlpha = 0.1f;
	float mTaaClampExpand = 0.05f;

	D3D12_GPU_DESCRIPTOR_HANDLE mTaaSrvTableBase[2] = {};

	DirectX::XMFLOAT2 mCurrJitterUV = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 mPrevJitterUV = { 0.0f, 0.0f };
	static const DXGI_FORMAT velocityFormat = DXGI_FORMAT_R16G16_FLOAT;

	int mGBufferSrvIndexAlbedo = -1;
	int mGBufferSrvIndexNormal = -1;
	int mGBufferSrvIndexPosition = -1;
	int mGBufferSrvIndexVelocity = -1; // пригодится дальше

	DirectX::XMFLOAT4X4 mPrevViewProjNoJitter = MathHelper::Identity4x4();

	float mTaaStrength = 1.0f;

	D3D12_RESOURCE_STATES mSceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	D3D12_RESOURCE_STATES mGBufferState[4] =
	{
		D3D12_RESOURCE_STATE_RENDER_TARGET, // Albedo
		D3D12_RESOURCE_STATE_RENDER_TARGET, // Normal
		D3D12_RESOURCE_STATE_RENDER_TARGET, // Position
		D3D12_RESOURCE_STATE_RENDER_TARGET  // Velocity
	};
	D3D12_RESOURCE_STATES mTaaHistState[2] =
	{
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	};
	D3D12_RESOURCE_STATES mTaaDepthHistState[2] =
	{
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	};
	D3D12_RESOURCE_STATES mDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
};

static const XMFLOAT2 gHalton23_8[8] =
{
	{0.5f,   1.0f / 3.0f},
	{0.25f,  2.0f / 3.0f},
	{0.75f,  1.0f / 9.0f},
	{0.125f, 4.0f / 9.0f},
	{0.625f, 7.0f / 9.0f},
	{0.375f, 2.0f / 9.0f},
	{0.875f, 5.0f / 9.0f},
	{0.0625f,8.0f / 9.0f},
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		TexColumnsApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

TexColumnsApp::TexColumnsApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

TexColumnsApp::~TexColumnsApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}
void TexColumnsApp::MoveBackFwd(float step) {
	XMFLOAT3 newPos;
	XMVECTOR fwd = cam.GetLook();
	XMStoreFloat3(&newPos, cam.GetPosition() + fwd * step);
	cam.SetPosition(newPos);
	cam.UpdateViewMatrix();
}
void TexColumnsApp::MoveLeftRight(float step) {
	XMFLOAT3 newPos;
	XMVECTOR right = cam.GetRight();
	XMStoreFloat3(&newPos, cam.GetPosition() + right * step);
	cam.SetPosition(newPos);
	cam.UpdateViewMatrix();
}
void TexColumnsApp::MoveUpDown(float step) {
	XMFLOAT3 newPos;
	XMVECTOR up = cam.GetUp();
	XMStoreFloat3(&newPos, cam.GetPosition() + up * step);
	cam.SetPosition(newPos);
	cam.UpdateViewMatrix();
}

bool TexColumnsApp::Initialize()
{
	// Создаем консольное окно.
	AllocConsole();

	// Перенаправляем стандартные потоки.
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);

	cam.SetPosition(0, 3, 10);
	cam.RotateY(MathHelper::Pi);
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	LoadAllTextures();
	BuildRootSignature();
	BuildLightingRootSignature();
	BuildShadowPassRootSignature();
	BuildPostProcessRootSignature();
	BuildTaaRootSignature();
	BuildLights();
	BuildShadowMapViews();
	BuildDescriptorHeaps();
	BuildShapeGeometry();
	SetLightShapes();
	BuildShadersAndInputLayout();
	BuildMaterials();
	BuildPSOs();
	BuildRenderItems();
	BuildDxrShadowRootSignature();
	BuildDxrShadowPSO();
	CreateDxrShadowMaskResources();
	BuildDxrAccelerationStructures();
	CreateDxrShadowDescriptors();
	BuildTerrainGeometry();
	mTerrain = std::make_unique<Terrain>();
	mTerrain->SetWorldSize(mTerrainWorldSize);
	mTerrain->SetHeightScale(mTerrainHeightScale);
	mTerrain->SetOriginY(mTerrainOriginY);
	mTerrain->SetLODDistances(mTerrainWorldSize * mTerrainLOD1Factor, mTerrainWorldSize * mTerrainLOD2Factor);
	mTerrain->BuildQuadtree();
	mTerrain->AssignHeightmapIndices(mTerrainHeightmapIndicesLOD0, mTerrainHeightmapIndicesLOD1, mTerrainHeightmapIndicesLOD2);
	BuildFrameResources();

	D3D12_DESCRIPTOR_HEAP_DESC imGuiHeapDesc = {};
	imGuiHeapDesc.NumDescriptors = 1;
	imGuiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	imGuiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	imGuiHeapDesc.NodeMask = 0; // Or the appropriate node mask if you have multiple GPUs
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&imGuiHeapDesc, IID_PPV_ARGS(&m_ImGuiSrvDescriptorHeap)));

	// INITIALIZE IMGUI ////////////////////
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	////////////////////////////////////////
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = md3dDevice.Get();
	init_info.CommandQueue = mCommandQueue.Get();
	init_info.NumFramesInFlight = gNumFrameResources;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; 
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	init_info.SrvDescriptorHeap = mSrvDescriptorHeap.Get();
	init_info.LegacySingleSrvCpuDescriptor = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	init_info.LegacySingleSrvGpuDescriptor = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	ImGui_ImplWin32_Init(mhMainWnd);
	ImGui_ImplDX12_Init(&init_info);
	mImGuiInitialized = true;
	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);


	FlushCommandQueue();
	return true;
}
void TexColumnsApp::CreateSceneTexture()
{
	mSceneTexture.Reset();

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = mClientWidth;
	texDesc.Height = mClientHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mBackBufferFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = mBackBufferFormat;
	memcpy(clearValue.Color, Colors::Black, sizeof(float) * 4);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearValue,
		IID_PPV_ARGS(&mSceneTexture)));

	mSceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	mSceneTexture->SetName(L"Scene Texture");

	// RTV index: swapchain + 4 gbuffer
	UINT sceneRtvIndex = SwapChainBufferCount + 4;
	mSceneRtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		sceneRtvIndex,
		mRtvDescriptorSize);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = mBackBufferFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	md3dDevice->CreateRenderTargetView(mSceneTexture.Get(), &rtvDesc, mSceneRtvHandle);
}


void TexColumnsApp::OnResize()
{
	D3DApp::OnResize();
	CreateGBuffer();
	auto p = mGBufferRTVs[3].ptr;
	auto s = mSceneRtvHandle.ptr;
	OutputDebugStringA(("gbuf3=" + std::to_string((uint64_t)p) + " scene=" + std::to_string((uint64_t)s) + "\n").c_str());

	CreateSceneTexture();
	CreateTaaHistoryTextures();   // <-- history ресурсы
	CreateTaaHistoryRtvs();       // <-- RTV для history 
	CreateTaaDepthHistoryTextures();
	BuildDescriptorHeaps();
	CreateDxrShadowMaskResources();
	CreateDxrShadowDescriptors();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.4f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	XMStoreFloat4x4(&mBaseProj, P);
	XMStoreFloat4x4(&mProj, P); 

	mJitterIndex = 0; 
}

void TexColumnsApp::Update(const GameTimer& gt)
{
	imguiID = 0;
	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
	UpdateCamera(gt);
	// ImGui Setup
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGui::Begin("Settings");
	ImGui::Text("Objects\n\n");

	// Velocity debug visualization modes
	ImGui::Combo("Velocity debug mode", &gVelocityDebugMode,
		"Off\0Moving objects (no camera)\0Velocity buffer (camera+objects)\0Velocity buffer object-only (approx)\0\0");

	Material* movingRedMat = nullptr;
	if (auto it = mMaterials.find("MovingRed"); it != mMaterials.end())
	{
		movingRedMat = it->second.get();
	}
	constexpr float kWorldDiffEps = 1e-4f;
	auto worldChanged = [&](const XMFLOAT4X4& a, const XMFLOAT4X4& b) -> bool
		{
			const float* pa = reinterpret_cast<const float*>(&a);
			const float* pb = reinterpret_cast<const float*>(&b);
			for (int i = 0; i < 16; ++i)
			{
				if (std::fabs(pa[i] - pb[i]) > kWorldDiffEps)
					return true;
			}
			return false;
		};

	for (auto& rItem : mAllRitems)
	{
		rItem->PrevWorld = rItem->World;
		if (rItem->Name == "nigga" || rItem->Name == "eyeL" || rItem->Name == "eyeR")
		{
			ImGui::Text(rItem->Name.c_str());
			ImGui::PushID(++imguiID);
			ImGui::DragFloat3("Position", (float*)&rItem->Position, 0.1f);

			ImGui::DragFloat3("Rotation", (float*)&rItem->RotationAngle, 0.05f);

			ImGui::DragFloat3("Scale", (float*)&rItem->Scale, 0.05f);

			if (rItem->Name == "nigga")
			{
				ImGui::Checkbox("Enable Movement", &enableMovement);
			}

			ImGui::PopID();

			// Circular movement logic for "nigga" object
			if (rItem->Name == "nigga" && enableMovement)
			{
				float radius = 5.0f;
				float speed = 2.0f;
				float angle = gt.TotalTime() * speed;
				rItem->Position.x = std::cos(angle) * radius;
				rItem->Position.z = std::sin(angle) * radius;
			}

			rItem->TranslationM = XMMatrixTranslation(rItem->Position.x, rItem->Position.y, rItem->Position.z);
			rItem->RotationM = XMMatrixRotationRollPitchYaw(rItem->RotationAngle.x, rItem->RotationAngle.y, rItem->RotationAngle.z);
			rItem->ScaleM = XMMatrixScaling(rItem->Scale.x, rItem->Scale.y, rItem->Scale.z);
			XMStoreFloat4x4(&rItem->World, rItem->ScaleM * rItem->RotationM * rItem->TranslationM);
			rItem->NumFramesDirty = gNumFrameResources;
		}

		// Mode 1: paint only moving objects (ignores camera motion).
		// Other modes: restore original material (velocity-based modes are handled in lighting shader).
		if (rItem->BaseMat == nullptr)
			rItem->BaseMat = rItem->Mat;

		if (gVelocityDebugMode == 1 && movingRedMat != nullptr)
		{
			const bool isMovingObj = worldChanged(rItem->PrevWorld, rItem->World);
			rItem->Mat = isMovingObj ? movingRedMat : rItem->BaseMat;
		}
		else
		{
			rItem->Mat = rItem->BaseMat;
		}
	}
	ImGui::Text("\n\nLights\n\n");
	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateLightCBs(gt);
	// post process update
	ImGui::End();
	ImGui::Begin("PostProcess Settings");
	ImGui::Text("Chromatic aberration");
	ImGui::DragFloat("Offset", &gChromaticAberrationOffset, 0.0001f);
	ImGui::Checkbox("Enable color correction", &CCenabled);
	ImGui::End();
	mChromaticAberrationCB->CopyData(0, gChromaticAberrationOffset);

	ImGui::Begin("TAA");
	//ImGui::DragFloat("Strength", &mTaaStrength, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Alpha", &mTaaAlpha, 0.001f, 0.0f, 1.0f);
	//ImGui::DragFloat("ClampExpand", &mTaaClampExpand, 0.001f, 0.0f, 1.0f);
	ImGui::End();

	ImGui::Begin("DXR Shadows");
	ImGui::Checkbox("Enable DXR (RayQuery) shadows", &mEnableDxrShadows);
	ImGui::Checkbox("Visualize shadow mask", &mVisualizeDxrShadowMask);
	ImGui::Checkbox("Update TLAS every frame", &mDxrUpdateTlasEveryFrame);
	int maxLightIdx = (int)mLights.size() - 1;
	if (maxLightIdx < 0) maxLightIdx = 0;
	ImGui::DragInt("DXR light index (LightCBIndex)", &mDxrShadowLightCBIndex, 1.0f, 0, maxLightIdx);
	{
		const char* items[] = { "1x (full)", "2x (half)", "4x (quarter)" };
		int cur = (mDxrShadowDownscale == 1) ? 0 : (mDxrShadowDownscale == 2) ? 1 : 2;
		if (ImGui::Combo("Mask downscale", &cur, items, IM_ARRAYSIZE(items)))
			mDxrShadowDownscale = (cur == 0) ? 1 : (cur == 1) ? 2 : 4;
	}
	ImGui::SliderInt("Samples / pixel", (int*)&mDxrShadowSamples, 1, 16);
	ImGui::SliderFloat("Cone angle (deg)", &mDxrConeAngleDeg, 0.0f, 5.0f, "%.2f");
	ImGui::DragFloat("Max distance", &mDxrMaxDistance, 10.0f, 1.0f, 20000.0f, "%.0f");
	ImGui::DragFloat("Normal bias", &mDxrNormalBias, 0.001f, 0.0f, 0.2f, "%.4f");
	ImGui::Text("BLAS: %zu  TLAS instances: %zu", mDxrBlas.size(), mDxrInstances.size());
	ImGui::Text("Mask: %s  TLAS: %s  PSO: %s",
		(mDxrShadowMask ? "OK" : "null"),
		(mDxrTlas ? "OK" : "null"),
		(mDxrShadowPSO ? "OK" : "null"));
	ImGui::End();

	// Recreate DXR shadow mask if downscale changed (expensive; only when user tweaks).
	if (mDxrShadowDownscale != mPrevDxrShadowDownscale)
	{
		mDxrShadowDownscale = (mDxrShadowDownscale <= 1) ? 1 : (mDxrShadowDownscale <= 2) ? 2 : 4;
		mPrevDxrShadowDownscale = mDxrShadowDownscale;
		FlushCommandQueue();
		CreateDxrShadowMaskResources();
		CreateDxrShadowDescriptors();
	}

	ImGui::Begin("Terrain");
	ImGui::Checkbox("Enable terrain", &mTerrainEnabled);
	ImGui::Checkbox("Wireframe (debug)", &mTerrainWireframe);
	ImGui::DragFloat("Origin Y (above spheres)", &mTerrainOriginY, 1.0f, -100.0f, 300.0f);
	ImGui::DragFloat("World size (XZ)", &mTerrainWorldSize, 1.0f, 10.0f, 500.0f);
	ImGui::DragFloat("Height scale", &mTerrainHeightScale, 0.5f, 1.0f, 200.0f);
	ImGui::DragFloat("LOD1 distance factor", &mTerrainLOD1Factor, 0.05f, 0.1f, 2.0f, "%.2f");
	ImGui::DragFloat("LOD2 distance factor", &mTerrainLOD2Factor, 0.05f, 0.05f, 1.0f, "%.2f");
	if (mTerrain)
		ImGui::Text("Visible tiles: %zu", mTerrain->GetVisibleTiles().size());
	ImGui::End();

	TAAConstants c = {};
	c.Alpha = mTaaAlpha;
	c.ClampExpand = mTaaClampExpand;
	c.InvRTSize = { 1.0f / mClientWidth, 1.0f / mClientHeight };
	c.TaaStrength = mTaaStrength;

	mTaaCB->CopyData(0, c);


	OutputDebugStringA(("alpha=" + std::to_string(mTaaAlpha) + "\n").c_str());

	UpdateMainPassCB(gt);
	if (mTerrain)
	{
		mTerrain->SetHeightScale(mTerrainHeightScale);
		mTerrain->SetOriginY(mTerrainOriginY);
		mTerrain->SetLODDistances(mTerrainWorldSize * mTerrainLOD1Factor, mTerrainWorldSize * mTerrainLOD2Factor);
		if (mTerrain->GetWorldSizeXZ() != mTerrainWorldSize)
		{
			mTerrain->SetWorldSize(mTerrainWorldSize);
			mTerrain->BuildQuadtree();
			mTerrain->AssignHeightmapIndices(mTerrainHeightmapIndicesLOD0, mTerrainHeightmapIndicesLOD1, mTerrainHeightmapIndicesLOD2);
		}
		mTerrain->Update(mMainPassCB.ViewProj, mMainPassCB.EyePosW);
	}

	// DXR shadow constants (updated every frame; TAA will filter noise)
	if (mEnableDxrShadows && mDxrShadowCB != nullptr)
	{
		if (!mLights.empty())
			mDxrShadowLightCBIndex = std::clamp(mDxrShadowLightCBIndex, 0, (int)mLights.size() - 1);
		else
			mDxrShadowLightCBIndex = 0;

		XMFLOAT3 lightDir = { 0.0f, -1.0f, 0.0f };
		bool found = false;

		// 1) Preferred: selected light index, if it's a directional light that casts shadows.
		for (const auto& l : mLights)
			if (l.LightCBIndex == mDxrShadowLightCBIndex && l.type == 2 && l.CastsShadows)
			{
				lightDir = l.Direction;
				found = true;
				break;
			}

		// 2) Fallback: first shadow-casting directional light.
		if (!found)
			for (const auto& l : mLights)
				if (l.type == 2 && l.CastsShadows)
				{
					lightDir = l.Direction;
					break;
				}

		DxrShadowConstants dxr = {};
		dxr.LightDirW = lightDir;
		dxr.ConeAngleRad = XMConvertToRadians(mDxrConeAngleDeg);
		dxr.SampleCount = max(1u, mDxrShadowSamples);
		dxr.FrameIndex = mDxrFrameIndex++;
		dxr.Downscale = (UINT)((mDxrShadowDownscale <= 1) ? 1 : (mDxrShadowDownscale <= 2) ? 2 : 4);
		dxr.MaxDistance = mDxrMaxDistance;
		dxr.NormalBias = mDxrNormalBias;
		mDxrShadowCB->CopyData(mCurrFrameResourceIndex, dxr);
	}
}


void TexColumnsApp::RotateSpotlightTowardCursor(int x, int y)
{
	float px = (2.0f * x) / mClientWidth - 1.0f;
	float py = 1.0f - (2.0f * y) / mClientHeight; // обратный y

	// 1. Получаем матрицы камеры
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX view = XMLoadFloat4x4(&mView); // используем саму камеру
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);

	// 2. NDC → View Space
	XMVECTOR rayClip = XMVectorSet(px, py, 1.0f, 1.0f); // z = 1
	XMVECTOR rayView = XMVector3TransformCoord(rayClip, invProj);
	rayView = XMVectorSetW(rayView, 0.0f); 

	// 3. View Space → World Space
	XMVECTOR rayDirWorld = XMVector3TransformNormal(rayView, invView);
	rayDirWorld = XMVector3Normalize(rayDirWorld);

	XMVECTOR rayOriginWorld = cam.GetPosition();
	XMVECTOR rayTarget = rayOriginWorld + rayDirWorld * 100.0f;

	// 4.
	for (auto& light : mLights)
	{
		if (light.type == 3) // spotlight
		{
			XMVECTOR lightPos = XMLoadFloat3(&light.Position);
			XMVECTOR dir = XMVector3Normalize(rayTarget - lightPos);

			// Вычисляем углы вращения
			float pitch = asinf(XMVectorGetY(dir)); // y
			float yaw = atan2f(XMVectorGetX(dir), XMVectorGetZ(dir)); // x/z

			light.Rotation.z = XMConvertToDegrees(-pitch - 3.14 / 2);
			light.Rotation.y = XMConvertToDegrees(yaw + 3.14 / 2);

			break;
		}
	}
}


void TexColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
	//if ((btnState & MK_LBUTTON) != 0 && !ImGui::GetIO().WantCaptureMouse)
	//{
	//	RotateSpotlightTowardCursor(x, y);
	//}
}

void TexColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void TexColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if (!ImGui::GetIO().WantCaptureMouse)
	{
		if ((btnState & MK_LBUTTON) != 0)
		{
			// Make each pixel correspond to a quarter of a degree.
			float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
			float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

			// Update angles based on input to orbit camera around box.

			cam.YawPitch(dx, -dy);

		}
		mLastMousePos.x = x;
		mLastMousePos.y = y;
	}
}


void TexColumnsApp::OnKeyPressed(const GameTimer& gt, WPARAM key)
{
	if (GET_WHEEL_DELTA_WPARAM(key) > 0 && !ImGui::GetIO().WantCaptureMouse)
	{
		cam.IncreaseSpeed(0.05);
	}
	else if (GET_WHEEL_DELTA_WPARAM(key) < 0 && !ImGui::GetIO().WantCaptureMouse)
	{
		cam.IncreaseSpeed(-0.05);
	}
	switch (key)
	{
	case 'A':
		MoveLeftRight(-cam.GetSpeed());
		return;
	case 'W':
		MoveBackFwd(cam.GetSpeed());
		return;
	case 'S':
		MoveBackFwd(-cam.GetSpeed());
		return;
	case 'D':
		MoveLeftRight(cam.GetSpeed());
		return;
	case 'Q':
		MoveUpDown(-cam.GetSpeed());
		return;
	case 'E':
		MoveUpDown(cam.GetSpeed());
		return;
	case VK_SHIFT:
		cam.SpeedUp();
		return;
	}

}

void TexColumnsApp::OnKeyReleased(const GameTimer& gt, WPARAM key)
{

	switch (key)
	{
	case VK_SHIFT:
		cam.SpeedDown();
		return;
	}
}

std::wstring TexColumnsApp::GetCamSpeed()
{
	return std::to_wstring(cam.GetSpeed());
}

void TexColumnsApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	float y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMVECTOR campos = cam.GetPosition();
	pos = XMVectorSet(campos.m128_f32[0], campos.m128_f32[1], campos.m128_f32[2], 0.0f);
	target = cam.GetLook();
	up = cam.GetUp();

	XMMATRIX view = XMMatrixLookToLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}




void TexColumnsApp::AnimateMaterials(const GameTimer& gt)
{

}

void TexColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();

	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.InvWorld, MathHelper::InverseTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			XMMATRIX prevWorld = XMLoadFloat4x4(&e->PrevWorld);
			XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(prevWorld));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}


void TexColumnsApp::UpdateLightCBs(const GameTimer& gt)
{
	auto currLightCB = mCurrFrameResource->LightCB.get();
	auto currShadowCB = mCurrFrameResource->PassShadowCB.get();

	// Day/Night cycle: animate first directional light (sun) when enabled
	if (mDayNightCycleEnabled && mDayNightCycleDuration > 0.01f)
	{
		const float twoPi = 6.283185307f;
		float angle = (float)fmod(gt.TotalTime() * twoPi / mDayNightCycleDuration, twoPi);
		// Sun path: angle 0 = east (rise), PI/2 = noon (up), PI = west (set), 3*PI/2 = night (below)
		float sunY = sinf(angle);  // elevation: 0 at horizon, 1 at zenith
		float sunX = cosf(angle);   // east-west
		XMFLOAT3 towardSun = { sunX, sunY, 0.0f };
		XMVECTOR v = XMLoadFloat3(&towardSun);
		v = XMVector3Normalize(v);
		XMFLOAT3 lightDir;
		XMStoreFloat3(&lightDir, -v); // light direction = from sun to scene

		for (auto& l : mLights)
			if (l.type == 2) {
				l.Direction = lightDir;
				break;
			}
	}

	int lId = 0;
	for (auto& l : mLights)
	{
		LightConstants lConst;
		PassShadowConstants shConst;
		if (l.type == 0)
		{
			//l.Color = mLights[0].Color; // ambient light equals directional;
			std::string s = "\Ambient Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			ImGui::DragFloat("Strength", (float*)&l.Strength, 0.02f);
			ImGui::PopID();

		}
		else if (l.type == 1)
		{
			std::string s = "\nPoint Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			float* a[] = { &l.Position.x,&l.Position.y,&l.Position.z };
			XMStoreFloat4x4(&l.gWorld, XMMatrixTranspose(XMMatrixScaling(l.FalloffEnd * 2, l.FalloffEnd * 2, l.FalloffEnd * 2) * XMMatrixTranslation(l.Position.x, l.Position.y, l.Position.z)));

			ImGui::DragFloat3("Position", *a, 0.1f, -100, 100);

			ImGui::ColorEdit3("Color", (float*)&l.Color);

			ImGui::DragFloat("Strength", &l.Strength, 0.1f, 0, 100);

			ImGui::DragFloat("FaloffStart", &l.FalloffStart, 0.1f, 1, l.FalloffEnd);

			ImGui::DragFloat("FaloffEnd", &l.FalloffEnd, 0.1f, l.FalloffStart, 100);

			bool b = l.isDebugOn;
			ImGui::Checkbox("is Debug On", &b);
			l.isDebugOn = b;

			ImGui::PopID();

			l.Position.z = sin(gt.TotalTime() * 3) * 6;
		}
		else if (l.type == 2)
		{
			std::string s = "\nDirectional Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			ImGui::SliderFloat3("Direction", (float*)&l.Direction, -1, 1);

			ImGui::ColorEdit3("Color", (float*)&l.Color);

			ImGui::DragFloat("Strength", &l.Strength, 0.1f, 0, 100);

			bool b = l.CastsShadows;
			ImGui::Checkbox("Cast Shadows", &b);
			l.CastsShadows = b;

			bool c = l.enablePCF;
			ImGui::Checkbox("Enable PCF", &c);
			l.enablePCF = c;

			ImGui::DragInt("PCF level", &l.pcf_level, 1, 0, 100);
			ImGui::DragFloat("Shadow softness (texels)", &l.ShadowSoftness, 0.5f, 0.0f, 32.0f, "%.1f");

			ImGui::PopID();

		}
		else if (l.type == 3)
		{
			std::string s = "\nSpot Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			float* a[] = { &l.Position.x,&l.Position.y,&l.Position.z };
			ImGui::DragFloat3("Position", (float*)&l.Position, 0.1f, -100, 100);

			ImGui::DragFloat3("Rotation", (float*)&l.Rotation, 0.1f, -180, 180);
			XMStoreFloat4x4(&l.gWorld, XMMatrixTranspose(XMMatrixScaling(l.FalloffEnd * 4 / 3, l.FalloffEnd, l.FalloffEnd * 4 / 3) * XMMatrixTranslation(0, -l.FalloffEnd / 2, 0) *
				XMMatrixRotationRollPitchYaw(XMConvertToRadians(l.Rotation.x), XMConvertToRadians(l.Rotation.y), XMConvertToRadians(l.Rotation.z)) *
				XMMatrixTranslation(l.Position.x, l.Position.y, l.Position.z)));
			XMFLOAT3 d(0, -1, 0);
			XMVECTOR v = XMLoadFloat3(&d);

			v = XMVector3TransformNormal(v, XMMatrixRotationRollPitchYaw(XMConvertToRadians(l.Rotation.x), XMConvertToRadians(l.Rotation.y), XMConvertToRadians(l.Rotation.z)));

			XMStoreFloat3(&l.Direction, v);
			d = XMFLOAT3(-1, 0, 0);
			v = XMLoadFloat3(&d);
			v = XMVector3TransformNormal(v, XMMatrixRotationRollPitchYaw(XMConvertToRadians(l.Rotation.x), XMConvertToRadians(l.Rotation.y), XMConvertToRadians(l.Rotation.z)));
			l.LightUp = v;

			ImGui::ColorEdit3("Color", (float*)&l.Color);

			ImGui::DragFloat("Strength", &l.Strength, 0.1f, 0, 100);

			ImGui::DragFloat("Faloff Start", &l.FalloffStart, 0.1f, 0, 100);

			ImGui::DragFloat("Faloff End", &l.FalloffEnd, 0.1f, 0, 100);

			ImGui::SliderFloat("Spot Power", &l.SpotPower, 0, 30);

			ImGui::DragInt("PCF level", &l.pcf_level, 1, 0, 100);

			bool c = l.enablePCF;
			ImGui::Checkbox("Enable PCF", &c);
			l.enablePCF = c;

			bool b = l.CastsShadows;
			ImGui::Checkbox("Cast Shadows", &b);
			l.CastsShadows = b;
			ImGui::DragFloat("Shadow softness (texels)", &l.ShadowSoftness, 0.5f, 0.0f, 32.0f, "%.1f");

			b = l.isDebugOn;
			ImGui::Checkbox("is Debug On", &b);
			l.isDebugOn = b;
			ImGui::PopID();


		}
		if (l.type == 2 && l.CastsShadows || l.type == 3 && l.CastsShadows) // Directional Light
		{
			// Create an orthographic projection for the directional light.
			// The volume needs to encompass the scene or relevant parts.
			// This is a simplified approach; Cascaded Shadow Maps (CSM) are better for large scenes.
			XMFLOAT3 Pos(l.Position);
			XMVECTOR lightPos = XMLoadFloat3(&Pos);
			XMVECTOR lightDir = XMLoadFloat3(&l.Direction);
			XMVECTOR targetPos = lightPos + lightDir; // Look at origin or scene center
			XMVECTOR lightUp = l.LightUp;

			XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
			XMStoreFloat4x4(&l.LightView, lightView);

			// Define the orthographic projection volume
			// These values depend heavily on your scene size.
			float viewWidth = 300.0f; // Adjust to fit your scene
			float viewHeight = 300.0f;
			float nearZ = 1.0f;
			float farZ = 1000.0f; // Adjust
			XMMATRIX lightProj = XMMatrixIdentity();
			if (l.type == 2)
				lightProj = XMMatrixOrthographicLH(viewWidth, viewHeight, nearZ, farZ);
			else
				lightProj = XMMatrixPerspectiveFovLH(0.5f * MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
			XMStoreFloat4x4(&l.LightProj, lightProj);
			XMStoreFloat4x4(&l.LightViewProj, XMMatrixTranspose(XMMatrixMultiply(lightView, lightProj)));
		}

		lConst.light = l;
		shConst.LightViewProj = l.LightViewProj;
		currShadowCB->CopyData(l.LightCBIndex, shConst);
		currLightCB->CopyData(l.LightCBIndex, lConst);
		lId++;
	}

	// Sync first directional light to atmosphere sun (for sky + reflections)
	for (const auto& l : mLights)
		if (l.type == 2) {
			XMVECTOR d = XMLoadFloat3(&l.Direction);
			XMStoreFloat3(&mAtmosphereParams.SunDirection, XMVector3Normalize(d));
			mAtmosphereParams.SunStrength = l.Strength;
			break;
		}

	// Atmosphere: real-time params (clean vs dirty)
	ImGui::Separator();
	ImGui::Text("Atmosphere (sky)");
	ImGui::Checkbox("Day/Night cycle (animate sun)", &mDayNightCycleEnabled);
	if (mDayNightCycleEnabled)
		ImGui::DragFloat("Day length (sec)", &mDayNightCycleDuration, 1.0f, 5.0f, 300.0f);
	ImGui::DragFloat("Rayleigh (0..1, blue/clean sky)", &mAtmosphereParams.Rayleigh, 0.02f, 0.0f, 2.0f);
	ImGui::DragFloat("Mie (0..1, haze)", &mAtmosphereParams.Mie, 0.02f, 0.0f, 1.0f);
	ImGui::DragFloat("Turbidity (1=clear, 2+=dirty)", &mAtmosphereParams.Turbidity, 0.05f, 0.5f, 5.0f);
	ImGui::DragFloat("Blend cubemap (0=atmosphere, 1=cubemap)", &mAtmosphereParams.BlendWithCubemap, 0.02f, 0.0f, 1.0f);

	if (mAtmosphereCB != nullptr)
		mAtmosphereCB->CopyData(0, mAtmosphereParams);
}

void TexColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{

		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			matConstants.Metallic = mat->Metallic;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TexColumnsApp::UpdateMainPassCB(const GameTimer& gt)
{
	// View / Proj базовые
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX baseProj = XMLoadFloat4x4(&mBaseProj);

	// Без jitter (для motion vectors)
	XMMATRIX viewProjNoJitter = XMMatrixMultiply(view, baseProj);

	// Jitter (Halton) -> NDC offset и UV offset
	XMFLOAT2 h = gHalton23_8[mJitterIndex];

	// NDC jitter (clip/NDC space translation)
	float jitterNdcX = (h.x - 0.5f) * (2.0f / (float)mClientWidth);
	float jitterNdcY = (h.y - 0.5f) * (2.0f / (float)mClientHeight);

	// UV jitter (то, что надо вычитать в velocity)
	XMFLOAT2 currJitterUV;
	currJitterUV.x = jitterNdcX * 0.5f;
	currJitterUV.y = -jitterNdcY * 0.5f; //UV y вниз

	// Jittered projection -> ViewProj (для растеризации/рендера)
	XMMATRIX jitterT = XMMatrixTranslation(jitterNdcX, jitterNdcY, 0.0f);
	XMMATRIX proj = XMMatrixMultiply(baseProj, jitterT);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	// Inverses
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	// Заполняем PassConstants (порядок полей должен совпадать с HLSL cbPass)
	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();


	// no-jitter матрицы (для velocity) 
	XMStoreFloat4x4(&mMainPassCB.ViewProjNoJitter, XMMatrixTranspose(viewProjNoJitter));
	XMStoreFloat4x4(&mMainPassCB.PrevViewProjNoJitter, XMMatrixTranspose(XMLoadFloat4x4(&mPrevViewProjNoJitter)));

	//prev viewproj (jittered) и jitters 
	XMStoreFloat4x4(&mMainPassCB.PrevViewProj, XMMatrixTranspose(XMLoadFloat4x4(&mPrevViewProj)));
	mMainPassCB.CurrJitterUV = currJitterUV;
	mMainPassCB.PrevJitterUV = mPrevJitterUV;

	// Заливка PassCB во frame resource
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);

	// TAA Reproject CB
	TAAReprojectConstants reproj;
	XMStoreFloat4x4(&reproj.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&reproj.PrevViewProj, XMMatrixTranspose(XMLoadFloat4x4(&mPrevViewProj))); // prev jittered VP
	mTaaReprojectCB->CopyData(0, reproj);

	// Сохраняем prev для следующего кадра 
	XMStoreFloat4x4(&mPrevViewProj, viewProj);               // jittered
	XMStoreFloat4x4(&mPrevViewProjNoJitter, viewProjNoJitter);

	mPrevJitterUV = currJitterUV;

	// индекс jitter
	mJitterIndex = (mJitterIndex + 1) % kJitterCount;
}




void TexColumnsApp::CreateGBuffer()
{
	const DXGI_FORMAT positionFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	const DXGI_FORMAT normalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	const DXGI_FORMAT albedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	const DXGI_FORMAT velocityFormat = DXGI_FORMAT_R16G16_FLOAT; // NEW

	FlushCommandQueue();
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = mClientWidth;
	texDesc.Height = mClientHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	mGBufferPosition.Reset();
	mGBufferNormal.Reset();
	mGBufferAlbedo.Reset();
	mGBufferVelocity.Reset(); // NEW

	// Position
	texDesc.Format = positionFormat;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&CD3DX12_CLEAR_VALUE(positionFormat, Colors::Black),
		IID_PPV_ARGS(&mGBufferPosition)));

	// Normal
	texDesc.Format = normalFormat;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&CD3DX12_CLEAR_VALUE(normalFormat, Colors::Black),
		IID_PPV_ARGS(&mGBufferNormal)));

	// Albedo
	texDesc.Format = albedoFormat;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&CD3DX12_CLEAR_VALUE(albedoFormat, Colors::Black),
		IID_PPV_ARGS(&mGBufferAlbedo)));

	// Velocity (clear = 0,0)
	texDesc.Format = velocityFormat;
	D3D12_CLEAR_VALUE velClear = {};
	velClear.Format = velocityFormat;
	velClear.Color[0] = 0.0f; velClear.Color[1] = 0.0f; velClear.Color[2] = 0.0f; velClear.Color[3] = 0.0f;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&velClear,
		IID_PPV_ARGS(&mGBufferVelocity)));

	// RTVs
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		SwapChainBufferCount,
		mRtvDescriptorSize);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	// Albedo RTV [0]
	rtvDesc.Format = albedoFormat;
	md3dDevice->CreateRenderTargetView(mGBufferAlbedo.Get(), &rtvDesc, rtvHandle);
	mGBufferRTVs[0] = rtvHandle;
	rtvHandle.Offset(1, mRtvDescriptorSize);

	// Normal RTV [1]
	rtvDesc.Format = normalFormat;
	md3dDevice->CreateRenderTargetView(mGBufferNormal.Get(), &rtvDesc, rtvHandle);
	mGBufferRTVs[1] = rtvHandle;
	rtvHandle.Offset(1, mRtvDescriptorSize);

	// Position RTV [2]
	rtvDesc.Format = positionFormat;
	md3dDevice->CreateRenderTargetView(mGBufferPosition.Get(), &rtvDesc, rtvHandle);
	mGBufferRTVs[2] = rtvHandle;
	rtvHandle.Offset(1, mRtvDescriptorSize);

	// Velocity RTV [3]  ✅ SwapChainBufferCount+3
	rtvDesc.Format = velocityFormat;
	md3dDevice->CreateRenderTargetView(mGBufferVelocity.Get(), &rtvDesc, rtvHandle);
	mGBufferRTVs[3] = rtvHandle;

	// IMPORTANT: init state tracker to match initial states of resources
	mGBufferState[0] = D3D12_RESOURCE_STATE_RENDER_TARGET; // Albedo
	mGBufferState[1] = D3D12_RESOURCE_STATE_RENDER_TARGET; // Normal
	mGBufferState[2] = D3D12_RESOURCE_STATE_RENDER_TARGET; // Position
	mGBufferState[3] = D3D12_RESOURCE_STATE_RENDER_TARGET; // Velocity

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();
}



void TexColumnsApp::LoadAllTextures()
{
	// MEGA COSTYL
	for (const auto& entry : std::filesystem::directory_iterator("../../Textures/textures"))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".dds")
		{
			std::string filepath = entry.path().string();
			filepath = filepath.substr(24, filepath.size());
			filepath = filepath.substr(0, filepath.size() - 4);
			filepath = "textures/" + filepath;
			LoadTexture(filepath);
		}
	}

	// PBR IBL textures + skybox (expected to be in ../../Textures/ or ../../Textures/textures/).
	// If they are missing, we just skip them (so the app doesn't crash building SRVs).
	auto tryLoad = [&](const std::string& texName)
		{
			if (mTextures.find(texName) == mTextures.end())
				LoadTexture(texName);
		};

	// Root Textures folder
	tryLoad("prefiltered");
	tryLoad("irradiance");
	tryLoad("brdfLUT");
	tryLoad("skybox");

	// Fallback: textures/ subfolder (if you placed them there)
	tryLoad("textures/prefiltered");
	tryLoad("textures/irradiance");
	tryLoad("textures/brdfLUT");
	tryLoad("textures/skybox");

	LoadTerrainTextures();
}

void TexColumnsApp::LoadTerrainTextures()
{
	auto tryLoad = [&](const std::string& name) {
		if (mTextures.find(name) == mTextures.end())
			LoadTexture(name);
	};
	tryLoad("001/Height_Out");
	tryLoad("002/Height/Height_Out_y0_x0");
	tryLoad("002/Height/Height_Out_y0_x1");
	tryLoad("002/Height/Height_Out_y1_x0");
	tryLoad("002/Height/Height_Out_y1_x1");
	for (int z = 0; z < 4; ++z)
		for (int x = 0; x < 4; ++x)
			tryLoad("003/Height/Height_Out_y" + std::to_string(z) + "_x" + std::to_string(x));
}

void TexColumnsApp::LoadTexture(const std::string& name)
{
	auto tex = std::make_unique<Texture>();
	tex->Name = name;
	tex->Filename = L"../../Textures/" + std::wstring(name.begin(), name.end()) + L".dds";

	if (!std::filesystem::exists(std::filesystem::path(tex->Filename)))
	{
		std::cout << "[LoadTexture] File not found: " << name << "\n";
		return;
	}

	if (FAILED(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tex->Filename.c_str(),
		tex->Resource, tex->UploadHeap)))
	{
		std::cout << "[LoadTexture] Failed to load: " << name << "\n";
		return;
	}
	mTextures[name] = std::move(tex);
}

void TexColumnsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE diffuseRange;
	diffuseRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // Диффузная текстура в регистре t0

	CD3DX12_DESCRIPTOR_RANGE normalRange;
	normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // Нормальная карта в регистре t1

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &diffuseRange, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsDescriptorTable(1, &normalRange, D3D12_SHADER_VISIBILITY_ALL);

	slotRootParameter[2].InitAsConstantBufferView(0); // register b0
	slotRootParameter[3].InitAsConstantBufferView(1); // register b1
	slotRootParameter[4].InitAsConstantBufferView(2); // register b2

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}


void TexColumnsApp::BuildLightingRootSignature()
{
	// t0 = Position, t1 = Normal, t2 = Albedo, t3 = ShadowMap, t4 = ShadowTexture, t5 = Velocity
	// t6 = Irradiance (cube), t7 = Prefiltered (cube), t8 = BRDF LUT (2D), t9 = Skybox (cube)

	CD3DX12_DESCRIPTOR_RANGE rAlbedo;
	rAlbedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

	CD3DX12_DESCRIPTOR_RANGE rNormal;
	rNormal.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1

	CD3DX12_DESCRIPTOR_RANGE rPosition;
	rPosition.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2

	CD3DX12_DESCRIPTOR_RANGE rShadowMap;
	rShadowMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // t3

	CD3DX12_DESCRIPTOR_RANGE rShadowTex;
	rShadowTex.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4); // t4

	CD3DX12_DESCRIPTOR_RANGE rVelocity;
	rVelocity.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5); // t5

	CD3DX12_DESCRIPTOR_RANGE rIrradiance;
	rIrradiance.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6); // t6

	CD3DX12_DESCRIPTOR_RANGE rPrefiltered;
	rPrefiltered.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7); // t7

	CD3DX12_DESCRIPTOR_RANGE rBrdfLut;
	rBrdfLut.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8); // t8

	CD3DX12_DESCRIPTOR_RANGE rSkybox;
	rSkybox.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9); // t9

	CD3DX12_ROOT_PARAMETER rootParams[15];
	rootParams[0].InitAsDescriptorTable(1, &rAlbedo, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParams[1].InitAsDescriptorTable(1, &rNormal, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParams[2].InitAsDescriptorTable(1, &rPosition, D3D12_SHADER_VISIBILITY_PIXEL);

	rootParams[3].InitAsConstantBufferView(0); // b0 = cbPass
	rootParams[4].InitAsConstantBufferView(1); // b1 = cbPerObject
	rootParams[5].InitAsConstantBufferView(2); // b2 = cbLight

	rootParams[6].InitAsDescriptorTable(1, &rShadowMap, D3D12_SHADER_VISIBILITY_PIXEL); // t3
	rootParams[7].InitAsDescriptorTable(1, &rShadowTex, D3D12_SHADER_VISIBILITY_PIXEL); // t4
	rootParams[8].InitAsDescriptorTable(1, &rVelocity, D3D12_SHADER_VISIBILITY_PIXEL);  // t5
	// b3: debug constants (root constants)
	rootParams[9].InitAsConstants(4, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParams[10].InitAsDescriptorTable(1, &rIrradiance, D3D12_SHADER_VISIBILITY_PIXEL);  // t6
	rootParams[11].InitAsDescriptorTable(1, &rPrefiltered, D3D12_SHADER_VISIBILITY_PIXEL); // t7
	rootParams[12].InitAsDescriptorTable(1, &rBrdfLut, D3D12_SHADER_VISIBILITY_PIXEL);     // t8
	rootParams[13].InitAsDescriptorTable(1, &rSkybox, D3D12_SHADER_VISIBILITY_PIXEL);      // t9
	rootParams[14].InitAsConstantBufferView(4); // b4 = cbAtmosphere

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(_countof(rootParams), rootParams,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob) ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mLightingRootSignature.GetAddressOf())));
}


// shadow root signature 
void TexColumnsApp::BuildShadowPassRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	slotRootParameter[0].InitAsConstantBufferView(0); // ObjectConstants (b0)
	slotRootParameter[1].InitAsConstantBufferView(1); // ShadowPassConstants (b1 - gLightViewProj)
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init(
		_countof(slotRootParameter), slotRootParameter,
		0, nullptr, // No static samplers needed for basic shadow map generation
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mShadowPassRootSignature)));
}

void TexColumnsApp::BuildPostProcessRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 for gSceneTexture
	CD3DX12_DESCRIPTOR_RANGE texTable2;
	texTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // 

	CD3DX12_ROOT_PARAMETER slotRootParameter[3];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0); // b0 for cbPostProcess
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_PIXEL); // b0 for cbPostProcess

	auto staticSamplers = GetStaticSamplers(); // Assuming you want to reuse existing samplers [cite: 1]
	// The ChromaticAberration.hlsl uses s0, so ensure your GetStaticSamplers()
	// provides a sampler at register s0 (like pointClamp or linearClamp).
	// The provided shader uses gsamLinearClamp at s0.
	// Your GetStaticSamplers() defines linearClamp at register s3. [cite: 2]
	// You should either change the shader to use s3 or adjust sampler registration here.
	// For now, let's assume the shader uses s3 for gsamLinearClamp.
	// Or, more simply, pass only the relevant sampler(s).

// For simplicity with the current ChromaticAberration.hlsl using s0:
	const CD3DX12_STATIC_SAMPLER_DESC linearClampSampler = CD3DX12_STATIC_SAMPLER_DESC(
		0, // shaderRegister (s0)
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);


	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter,
		1, &linearClampSampler, // Use only the one sampler needed
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mPostProcessRootSignature.GetAddressOf())));
}

void TexColumnsApp::BuildDxrShadowRootSignature()
{
	// DXR 1.1 inline ray tracing compute root signature:
	// t0 = TLAS, t1 = Position, t2 = Normal, u0 = ShadowMask, b0 = constants
	CD3DX12_DESCRIPTOR_RANGE tlasRange;
	tlasRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE posRange;
	posRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	CD3DX12_DESCRIPTOR_RANGE nrmRange;
	nrmRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
	CD3DX12_DESCRIPTOR_RANGE outRange;
	outRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER params[5];
	params[0].InitAsDescriptorTable(1, &tlasRange);
	params[1].InitAsDescriptorTable(1, &posRange);
	params[2].InitAsDescriptorTable(1, &nrmRange);
	params[3].InitAsDescriptorTable(1, &outRange);
	params[4].InitAsConstantBufferView(0);

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(_countof(params), params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> serialized = nullptr;
	ComPtr<ID3DBlob> errors = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errors.GetAddressOf());
	if (errors) ::OutputDebugStringA((char*)errors->GetBufferPointer());
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0,
		serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(mDxrShadowRootSignature.GetAddressOf())));
}

void TexColumnsApp::BuildDxrShadowPSO()
{
	// Check DXR support.
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
	if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))) ||
		opts5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
	{
		mEnableDxrShadows = false;
		return;
	}

	// RayQuery requires SM 6.5.
	D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_5 };
	if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) ||
		sm.HighestShaderModel < D3D_SHADER_MODEL_6_5)
	{
		mEnableDxrShadows = false;
		return;
	}

	// Compile RayQuery compute shader with DXC (cs_6_5).
	// If DXC is missing at runtime (dxcompiler.dll), fall back to shadow maps instead of crashing.
	try
	{
		mDxrShadowCS = CompileShaderDXC(L"Shaders\\DxrShadowMask.hlsl", L"CS", L"cs_6_5");

		D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
		pso.pRootSignature = mDxrShadowRootSignature.Get();
		pso.CS = { (BYTE*)mDxrShadowCS->GetBufferPointer(), mDxrShadowCS->GetBufferSize() };
		ThrowIfFailed(md3dDevice->CreateComputePipelineState(&pso, IID_PPV_ARGS(&mDxrShadowPSO)));
	}
	catch (const DxException&)
	{
		mEnableDxrShadows = false;
		mDxrShadowCS = nullptr;
		mDxrShadowPSO = nullptr;
	}
}
void TexColumnsApp::CreatePointLight(XMFLOAT3 pos, XMFLOAT3 color, float faloff_start, float faloff_end, float strength)
{
	Light light = {};
	light.LightCBIndex = mLights.size();

	light.Position = pos;
	light.Color = color;
	light.FalloffStart = faloff_start;
	light.FalloffEnd = faloff_end;
	light.type = 1;
	light.Strength = strength;
	light.CastsShadows = false;
	light.enablePCF = false;
	light.pcf_level = 0;
	light.isDebugOn = false;
	auto& world = XMMatrixScaling(faloff_end * 2, faloff_end * 2, faloff_end * 2) * XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMStoreFloat4x4(&light.gWorld, XMMatrixTranspose(world));
	mLights.push_back(light);
}
void TexColumnsApp::CreateSpotLight(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 color, float faloff_start, float faloff_end, float strength, float spotpower)
{
	Light light = {};
	light.LightCBIndex = mLights.size();

	light.Position = pos;
	light.Color = color;
	light.FalloffStart = faloff_start;
	light.FalloffEnd = faloff_end;
	light.Rotation = rot;
	light.LightUp = XMVectorSet(0, 1, 0, 0);
	light.type = 3;
	light.Strength = strength;
	light.SpotPower = spotpower;
	light.CastsShadows = false;
	light.enablePCF = false;
	light.pcf_level = 0;
	light.isDebugOn = false;
	mLights.push_back(light);
}

void TexColumnsApp::BuildLights()
{
	// Directional 1 (main sun) — включаем тени, чтобы видеть RT-тени
	Light dir = {};
	dir.LightCBIndex = mLights.size();
	dir.Position = { 0, 300, 0 };
	dir.Direction = { 0.2f, -0.95f, 0.2f };
	dir.Color = { 1, 1, 1 };
	dir.Strength = 3.0f;
	dir.CastsShadows = true;
	dir.enablePCF = false;
	dir.pcf_level = 0;
	dir.ShadowSoftness = 6.0f;
	dir.isDebugOn = false;
	dir.type = 2;
	dir.LightUp = XMVectorSet(0, 0, -1, 0);
	XMStoreFloat4x4(&dir.gWorld, XMMatrixTranspose(XMMatrixScaling(1000, 1000, 1000)));
	mLights.push_back(dir);

	// Directional 2 (второй источник — сбоку, чтобы видеть вторую тень)
	Light dir2 = {};
	dir2.LightCBIndex = mLights.size();
	dir2.Position = { 150, 80, 150 };
	dir2.Direction = { -0.7f, -0.5f, -0.5f };
	dir2.Color = { 0.9f, 0.85f, 0.7f };
	dir2.Strength = 1.5f;
	dir2.CastsShadows = true;
	dir2.enablePCF = false;
	dir2.pcf_level = 0;
	dir2.ShadowSoftness = 4.0f;
	dir2.isDebugOn = false;
	dir2.type = 2;
	dir2.LightUp = XMVectorSet(0, 1, 0, 0);
	XMStoreFloat4x4(&dir2.gWorld, XMMatrixTranspose(XMMatrixScaling(1000, 1000, 1000)));
	mLights.push_back(dir2);

	Light ambient = {};
	ambient.LightCBIndex = mLights.size();
	ambient.Position = { 3.0f, 0.0f, 3.0f };
	ambient.Color = { 0,0,0 }; // need only x
	ambient.Strength = 0.3; // need only x
	ambient.CastsShadows = false;
	ambient.enablePCF = false;
	ambient.pcf_level = 0;
	ambient.isDebugOn = false;
	ambient.type = 0;
	XMStoreFloat4x4(&ambient.gWorld, XMMatrixTranspose(XMMatrixTranslation(0, 0, 0) * XMMatrixScaling(1000, 1000, 1000)));
	mLights.push_back(ambient);

	CreatePointLight({ -3,3,0 }, { 4,0,0 }, 1, 5, 1);
	CreatePointLight({ 3,3,0 }, { 0,0,4 }, 1, 5, 1);

	CreateSpotLight({ -5,3,30 }, { 0,0,-90 }, { 1,1,1 }, 1, 30, 15, 20);
}

void TexColumnsApp::SetLightShapes()
{
	for (auto& light : mLights)
	{

		switch (light.type)
		{
		case 1:
			light.ShapeGeo = mGeometries["shapeGeo"]->DrawArgs["sphere"];
			break;
		case 3:
			light.ShapeGeo = mGeometries["shapeGeo"]->DrawArgs["box"];
			break;
		}
	}
	mLights;
}

void TexColumnsApp::CreateMaterial(std::string _name, int _CBIndex, int _SRVDiffIndex, int _SRVNMapIndex, XMFLOAT4 _DiffuseAlbedo, XMFLOAT3 _FresnelR0, float _Roughness, float _Metallic)
{

	auto material = std::make_unique<Material>();
	material->Name = _name;
	material->MatCBIndex = static_cast<int>(mMaterials.size());
	material->DiffuseSrvHeapIndex = _SRVDiffIndex;
	material->NormalSrvHeapIndex = _SRVNMapIndex;
	material->DiffuseAlbedo = _DiffuseAlbedo;
	material->FresnelR0 = _FresnelR0;
	material->Roughness = _Roughness;
	material->Metallic = _Metallic;
	mMaterials[_name] = std::move(material);
}

void TexColumnsApp::BuildShadowMapViews()
{
	int shadowMapCount = 0;
	for (const auto& light : mLights)
		if (light.type == 2 || light.type == 3)
			shadowMapCount++;
	shadowMapCount = max(shadowMapCount, 1);

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = shadowMapCount;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));
	int i = 0;
	for (auto& light : mLights)
	{

		if (light.type == 2 || light.type == 3)
		{
			// Define shadow map properties (can be members of the class or taken from a specific light)
			mShadowViewport = { 0.0f, 0.0f, (float)SHADOW_MAP_WIDTH, (float)SHADOW_MAP_HEIGHT, 0.0f, 1.0f };
			mShadowScissorRect = { 0, 0, (int)SHADOW_MAP_WIDTH, (int)SHADOW_MAP_HEIGHT };

			// Create the shadow map texture
			D3D12_RESOURCE_DESC texDesc;
			ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
			texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			texDesc.Alignment = 0;
			texDesc.Width = SHADOW_MAP_WIDTH;
			texDesc.Height = SHADOW_MAP_HEIGHT;
			texDesc.DepthOrArraySize = 1;
			texDesc.MipLevels = 1;
			texDesc.Format = SHADOW_MAP_FORMAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			D3D12_CLEAR_VALUE clearValue;
			clearValue.Format = SHADOW_MAP_DSV_FORMAT;
			clearValue.DepthStencil.Depth = 1.0f;
			clearValue.DepthStencil.Stencil = 0;

			ThrowIfFailed(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				&clearValue,
				IID_PPV_ARGS(&light.ShadowMap)));


			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
			dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Format = SHADOW_MAP_DSV_FORMAT;
			dsvDesc.Texture2D.MipSlice = 0;
			light.ShadowMapDsvHandle = mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
			light.ShadowMapDsvHandle.Offset(i, mDsvDescriptorSize); // Use the stored index
			md3dDevice->CreateDepthStencilView(light.ShadowMap.Get(), &dsvDesc, light.ShadowMapDsvHandle);

			light.ShadowMapSrvHeapIndex = mTextures.size() + 3 + i;
			i++;
		}
	}
	//std::cout << mLights.size();

}

void TexColumnsApp::BuildDescriptorHeaps()
{
	// =========================================================
	// Layout (SRV heap indices):
	// [0 .. texturesCount-1]                       : all textures
	// [texturesCount .. texturesCount+2]           : GBuffer SRVs (Albedo, Normal, Position)
	// [after that .. +shadowCount-1]               : Shadow maps SRVs (packed contiguous)
	// [TAA table 0: 4 SRVs contiguous]             : [Scene][Hist0][DepthCur][PrevDepth0]
	// [TAA table 1: 4 SRVs contiguous]             : [Scene][Hist1][DepthCur][PrevDepth1]
	// [DXR (3 descriptors)]                        : [TLAS SRV][ShadowMask UAV][ShadowMask SRV]
	// =========================================================

	// 0) Count shadow SRVs
	int shadowCount = 0;
	for (auto& light : mLights)
		if (light.CastsShadows)
			shadowCount++;

	const int texturesCount = (int)mTextures.size();
	const int kGbufferCount = 4; // Albedo, Normal, Position, Velocity
	const int kTaaCount = 10; // 2 tables * 5 SRVs
	const int kDxrCount = 3; // TLAS SRV + ShadowMask UAV + ShadowMask SRV


	const int baseTextures = 0;
	const int baseGbuffer = baseTextures + texturesCount;

	mGBufferSrvIndexAlbedo = baseGbuffer + 0;
	mGBufferSrvIndexNormal = baseGbuffer + 1;
	mGBufferSrvIndexPosition = baseGbuffer + 2;
	mGBufferSrvIndexVelocity = baseGbuffer + 3;


	const int baseShadows = baseGbuffer + kGbufferCount;
	const int baseTaa = baseShadows + shadowCount;
	const int baseDxr = baseTaa + kTaaCount;

	// table0
	const int taa0_scene = baseTaa + 0;
	const int taa0_hist = baseTaa + 1;
	const int taa0_depthCur = baseTaa + 2;
	const int taa0_prevDepth = baseTaa + 3;
	const int taa0_velocity = baseTaa + 4;

	// table1
	const int taa1_scene = baseTaa + 5;
	const int taa1_hist = baseTaa + 6;
	const int taa1_depthCur = baseTaa + 7;
	const int taa1_prevDepth = baseTaa + 8;
	const int taa1_velocity = baseTaa + 9;

	const int totalSrvCount = texturesCount + kGbufferCount + shadowCount + kTaaCount + kDxrCount;

	// DXR descriptor indices (filled later when resources exist)
	mDxrTlasSrvIndex = baseDxr + 0;
	mDxrShadowMaskUavIndex = baseDxr + 1;
	mDxrShadowMaskSrvIndex = baseDxr + 2;

	// 1) Create SRV heap (only if needed). Recreating it after ImGui init can break ImGui.
	bool needCreate = true;
	if (mSrvDescriptorHeap)
	{
		auto existing = mSrvDescriptorHeap->GetDesc();
		if (existing.NumDescriptors >= (UINT)totalSrvCount)
			needCreate = false;
	}

	if (needCreate)
	{
		// If ImGui is already initialized, avoid recreating the heap (it will hold stale handles).
		if (mImGuiInitialized)
		{
			// Fall back: keep the old heap and disable DXR features that add descriptors.
			mEnableDxrShadows = false;
			// Recompute without DXR descriptors.
			// Note: this should not happen in normal flow; SRV heap should be sized once after textures load.
			// (We still continue and overwrite views into the existing heap.)
		}
		else
		{
			D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
			srvHeapDesc.NumDescriptors = (UINT)totalSrvCount;
			srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));
		}
	}

	auto cpuAt = [&](int idx)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
			h.Offset(idx, mCbvSrvDescriptorSize);
			return h;
		};

	auto gpuAt = [&](int idx)
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			h.Offset(idx, mCbvSrvDescriptorSize);
			return h;
		};

	// 2) Regular textures SRVs
	int texIndex = 0;
	auto isCubeName = [&](const std::string& name) -> bool
		{
			std::string s = name;
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });

			return (s.find("cube") != std::string::npos) ||
				(s.find("skybox") != std::string::npos) ||
				(s.find("irradiance") != std::string::npos) ||
				(s.find("prefilter") != std::string::npos);
		};

	for (const auto& kv : mTextures)
	{
		auto res = kv.second->Resource;
		auto texDesc = res->GetDesc();
		DXGI_FORMAT format = texDesc.Format;
		if (format == DXGI_FORMAT_UNKNOWN)
			abort();

		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Format = format;

		if (texDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
		{
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MostDetailedMip = 0;
			desc.Texture3D.MipLevels = texDesc.MipLevels;
			desc.Texture3D.ResourceMinLODClamp = 0.0f;
		}
		else if (texDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
		{
			const UINT arraySize = texDesc.DepthOrArraySize;

			// Cubemap(s)
			const bool cubeCandidate = (arraySize >= 6) && ((arraySize % 6) == 0) && isCubeName(kv.first);
			if (cubeCandidate)
			{
				if (arraySize == 6)
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
					desc.TextureCube.MostDetailedMip = 0;
					desc.TextureCube.MipLevels = texDesc.MipLevels;
					desc.TextureCube.ResourceMinLODClamp = 0.0f;
				}
				else
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
					desc.TextureCubeArray.MostDetailedMip = 0;
					desc.TextureCubeArray.MipLevels = texDesc.MipLevels;
					desc.TextureCubeArray.First2DArrayFace = 0;
					desc.TextureCubeArray.NumCubes = arraySize / 6;
					desc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
				}
			}
			else if (arraySize > 1)
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				desc.Texture2DArray.MostDetailedMip = 0;
				desc.Texture2DArray.MipLevels = texDesc.MipLevels;
				desc.Texture2DArray.FirstArraySlice = 0;
				desc.Texture2DArray.ArraySize = arraySize;
				desc.Texture2DArray.PlaneSlice = 0;
				desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
			}
			else
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MostDetailedMip = 0;
				desc.Texture2D.MipLevels = texDesc.MipLevels;
				desc.Texture2D.PlaneSlice = 0;
				desc.Texture2D.ResourceMinLODClamp = 0.0f;
			}
		}
		else
		{
			// Fallback: treat as a simple 2D texture SRV.
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MostDetailedMip = 0;
			desc.Texture2D.MipLevels = texDesc.MipLevels;
			desc.Texture2D.PlaneSlice = 0;
			desc.Texture2D.ResourceMinLODClamp = 0.0f;
		}

		md3dDevice->CreateShaderResourceView(res.Get(), &desc, cpuAt(baseTextures + texIndex));

		TexOffsets[kv.first] = texIndex;
		texIndex++;
	}

	// Terrain heightmap indices for quadtree LOD
	mTerrainHeightmapIndicesLOD0.clear();
	mTerrainHeightmapIndicesLOD1.clear();
	mTerrainHeightmapIndicesLOD2.clear();
	auto texIdx = [&](const std::string& name) -> int {
		auto it = TexOffsets.find(name);
		return (it != TexOffsets.end()) ? it->second : -1;
	};
	if (texIdx("001/Height_Out") >= 0)
		mTerrainHeightmapIndicesLOD0.push_back(texIdx("001/Height_Out"));
	for (int z = 0; z < 2; ++z)
		for (int x = 0; x < 2; ++x) {
			std::string n = "002/Height/Height_Out_y" + std::to_string(z) + "_x" + std::to_string(x);
			if (texIdx(n) >= 0) mTerrainHeightmapIndicesLOD1.push_back(texIdx(n));
		}
	for (int z = 0; z < 4; ++z)
		for (int x = 0; x < 4; ++x) {
			std::string n = "003/Height/Height_Out_y" + std::to_string(z) + "_x" + std::to_string(x);
			if (texIdx(n) >= 0) mTerrainHeightmapIndicesLOD2.push_back(texIdx(n));
		}
	// Fallback heightmap when 001/002/003 not loaded (so terrain still draws)
	int fallback = texIdx("textures/HeightMap2");
	if (fallback < 0) fallback = texIdx("textures/HeightMap");
	if (fallback < 0 && !TexOffsets.empty()) fallback = TexOffsets.begin()->second;
	mTerrainFallbackHeightmapIndex = (fallback >= 0) ? fallback : -1;
	if (mTerrainHeightmapIndicesLOD0.empty() && mTerrainFallbackHeightmapIndex >= 0)
		mTerrainHeightmapIndicesLOD0.push_back(mTerrainFallbackHeightmapIndex);

	// 3) GBuffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC gbufSrvDesc = {};
	gbufSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	gbufSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	gbufSrvDesc.Texture2D.MipLevels = 1;
	gbufSrvDesc.Texture2D.MostDetailedMip = 0;
	gbufSrvDesc.Texture2D.PlaneSlice = 0;
	gbufSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	gbufSrvDesc.Format = albedoFormat;
	md3dDevice->CreateShaderResourceView(mGBufferAlbedo.Get(), &gbufSrvDesc, cpuAt(baseGbuffer + 0));

	gbufSrvDesc.Format = normalFormat;
	md3dDevice->CreateShaderResourceView(mGBufferNormal.Get(), &gbufSrvDesc, cpuAt(baseGbuffer + 1));

	gbufSrvDesc.Format = positionFormat;
	md3dDevice->CreateShaderResourceView(mGBufferPosition.Get(), &gbufSrvDesc, cpuAt(baseGbuffer + 2));

	// Velocity SRV (отдельным desc)
	D3D12_SHADER_RESOURCE_VIEW_DESC velSrvDesc = {};
	velSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	velSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	velSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	velSrvDesc.Texture2D.MostDetailedMip = 0;
	velSrvDesc.Texture2D.MipLevels = 1;
	velSrvDesc.Texture2D.PlaneSlice = 0;
	velSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	md3dDevice->CreateShaderResourceView(mGBufferVelocity.Get(), &velSrvDesc, cpuAt(baseGbuffer + 3));



	// 4) Shadow SRVs (packed contiguous!) + update light indices
	D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
	shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shadowSrvDesc.Format = SHADOW_MAP_SRV_FORMAT;
	shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	shadowSrvDesc.Texture2D.MostDetailedMip = 0;
	shadowSrvDesc.Texture2D.MipLevels = 1;
	shadowSrvDesc.Texture2D.PlaneSlice = 0;
	shadowSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	int shadowWrite = 0;
	for (auto& light : mLights)
	{
		if (!light.CastsShadows)
			continue;

		const int idx = baseShadows + shadowWrite;

		light.ShadowMapSrvHeapIndex = idx;

		md3dDevice->CreateShaderResourceView(light.ShadowMap.Get(), &shadowSrvDesc, cpuAt(idx));

		shadowWrite++;
	}


	// 5) TAA tables ( t0..t4)

	// SRV desc for color buffers (Scene/History)
	D3D12_SHADER_RESOURCE_VIEW_DESC colorSrvDesc = {};
	colorSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	colorSrvDesc.Format = mBackBufferFormat;
	colorSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	colorSrvDesc.Texture2D.MostDetailedMip = 0;
	colorSrvDesc.Texture2D.MipLevels = 1;
	colorSrvDesc.Texture2D.PlaneSlice = 0;
	colorSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	// SRV desc for depth buffers (R24_UNORM_X8)
	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
	depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrvDesc.Texture2D.MostDetailedMip = 0;
	depthSrvDesc.Texture2D.MipLevels = 1;
	depthSrvDesc.Texture2D.PlaneSlice = 0;
	depthSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;


	// Table bases for root descriptor table binding
	mTaaSrvTableBase[0] = gpuAt(taa0_scene);
	mTaaSrvTableBase[1] = gpuAt(taa1_scene);

	mSceneSrvHeapIndex = taa0_scene;
	mSceneSrvHandle = gpuAt(taa0_scene);

	mTaaHistorySrv[0] = gpuAt(taa0_hist);
	mTaaHistorySrv[1] = gpuAt(taa1_hist);

	mDepthSrvHandle = gpuAt(taa0_depthCur);
	mTaaDepthHistorySrv[0] = gpuAt(taa0_prevDepth);
	mTaaDepthHistorySrv[1] = gpuAt(taa1_prevDepth);

	// -------- table0: scene, hist0, depthCur, prevDepth0, velocity
	md3dDevice->CreateShaderResourceView(mSceneTexture.Get(), &colorSrvDesc, cpuAt(taa0_scene));
	md3dDevice->CreateShaderResourceView(mTaaHistory[0].Get(), &colorSrvDesc, cpuAt(taa0_hist));
	md3dDevice->CreateShaderResourceView(mDepthStencilBuffer.Get(), &depthSrvDesc, cpuAt(taa0_depthCur));
	md3dDevice->CreateShaderResourceView(mTaaDepthHistory[0].Get(), &depthSrvDesc, cpuAt(taa0_prevDepth));
	md3dDevice->CreateShaderResourceView(mGBufferVelocity.Get(), &velSrvDesc, cpuAt(taa0_velocity));

	// -------- table1: scene, hist1, depthCur, prevDepth1, velocity
	md3dDevice->CreateShaderResourceView(mSceneTexture.Get(), &colorSrvDesc, cpuAt(taa1_scene));
	md3dDevice->CreateShaderResourceView(mTaaHistory[1].Get(), &colorSrvDesc, cpuAt(taa1_hist));
	md3dDevice->CreateShaderResourceView(mDepthStencilBuffer.Get(), &depthSrvDesc, cpuAt(taa1_depthCur));
	md3dDevice->CreateShaderResourceView(mTaaDepthHistory[1].Get(), &depthSrvDesc, cpuAt(taa1_prevDepth));
	md3dDevice->CreateShaderResourceView(mGBufferVelocity.Get(), &velSrvDesc, cpuAt(taa1_velocity));

	// 6) Debug
	HRESULT hr = md3dDevice->GetDeviceRemovedReason();
	if (FAILED(hr))
		std::cout << "DeviceRemovedReason: 0x" << std::hex << hr << std::endl;
}



void TexColumnsApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["gbufferVS"] = d3dUtil::CompileShader(L"Shaders\\GeometryPass.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["gbufferPS"] = d3dUtil::CompileShader(L"Shaders\\GeometryPass.hlsl", nullptr, "PS", "ps_5_0");
	mShaders["lightingVS"] = d3dUtil::CompileShader(L"Shaders\\PBRLightingPass.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["lightingQUADVS"] = d3dUtil::CompileShader(L"Shaders\\PBRLightingPass.hlsl", nullptr, "VS_QUAD", "vs_5_0");
	mShaders["lightingPS"] = d3dUtil::CompileShader(L"Shaders\\PBRLightingPass.hlsl", nullptr, "PS", "ps_5_0");
	mShaders["lightingPSDebug"] = d3dUtil::CompileShader(L"Shaders\\PBRLightingPass.hlsl", nullptr, "PS_debug", "ps_5_0");
	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\ShadowMap.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["postprocessVS"] = d3dUtil::CompileShader(L"Shaders\\PostProcess.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["postprocessPS"] = d3dUtil::CompileShader(L"Shaders\\PostProcess.hlsl", nullptr, "PS", "ps_5_0");
	mShaders["taaResolveVS"] = d3dUtil::CompileShader(L"Shaders\\TAAResolve.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["taaResolvePS"] = d3dUtil::CompileShader(L"Shaders\\TAAResolve.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["terrainVS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["terrainPS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}
void TexColumnsApp::BuildCustomMeshGeometry(std::string name, UINT& meshVertexOffset, UINT& meshIndexOffset, UINT& prevVertSize, UINT& prevIndSize, std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, MeshGeometry* Geo)
{
	std::vector<GeometryGenerator::MeshData> meshDatas; // Это твоя структура для хранения вершин и индексов

	// Создаем инстанс импортера.
	Assimp::Importer importer;

	// Читаем файл с постпроцессингом: триангуляция, флип UV (если нужно) и генерация нормалей.
	const aiScene* scene = importer.ReadFile("../../Common/" + name + ".obj",
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_FlipUVs |
		aiProcess_GenNormals |
		aiProcess_CalcTangentSpace);
	if (!scene || !scene->mRootNode)
	{
		std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
	}
	unsigned int nMeshes = scene->mNumMeshes;
	ObjectsMeshCount[name] = nMeshes;

	for (int i = 0; i < scene->mNumMeshes; i++)
	{
		GeometryGenerator::MeshData meshData;
		aiMesh* mesh = scene->mMeshes[i];

		// Подготовка контейнеров для вершин и индексов.
		std::vector<GeometryGenerator::Vertex> vertices;
		std::vector<std::uint16_t> indices;

		// Проходим по всем вершинам и копируем данные.
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
		{
			GeometryGenerator::Vertex v;

			v.Position.x = mesh->mVertices[i].x;
			v.Position.y = mesh->mVertices[i].y;
			v.Position.z = mesh->mVertices[i].z;

			if (mesh->HasNormals())
			{
				v.Normal.x = mesh->mNormals[i].x;
				v.Normal.y = mesh->mNormals[i].y;
				v.Normal.z = mesh->mNormals[i].z;
			}

			if (mesh->HasTextureCoords(0))
			{
				v.TexC.x = mesh->mTextureCoords[0][i].x;
				v.TexC.y = mesh->mTextureCoords[0][i].y;
			}
			else
			{
				v.TexC = XMFLOAT2(0.0f, 0.0f);
			}
			if (mesh->HasTangentsAndBitangents())
			{
				v.TangentU.x = mesh->mTangents[i].x;
				v.TangentU.y = mesh->mTangents[i].y;
				v.TangentU.z = mesh->mTangents[i].z;

			}

			// Если необходимо, можно обработать тангенты и другие атрибуты.
			vertices.push_back(v);
		}
		// Проходим по всем граням для формирования индексов.
		for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
		{
			aiFace face = mesh->mFaces[i];
			// Убедимся, что грань треугольная.
			if (face.mNumIndices != 3) continue;
			indices.push_back(static_cast<std::uint16_t>(face.mIndices[0]));
			indices.push_back(static_cast<std::uint16_t>(face.mIndices[1]));
			indices.push_back(static_cast<std::uint16_t>(face.mIndices[2]));
		}

		meshData.Vertices = vertices;
		meshData.Indices32.resize(indices.size());
		for (size_t j = 0; j < indices.size(); ++j)
			meshData.Indices32[j] = indices[j];

		aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
		aiString texturePath;

		aiString texPath;

		meshData.matName = scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str();
		meshDatas.push_back(meshData);
	}
	for (int k = 0; k < scene->mNumMaterials; k++)
	{
		aiString texPath;
		scene->mMaterials[k]->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
		std::string a = std::string(texPath.C_Str());
		a = a.substr(0, a.length() - 4);
		std::cout << "DIFFUSE: " << a << "\n";
		scene->mMaterials[k]->GetTexture(aiTextureType_DISPLACEMENT, 0, &texPath);
		std::string b = std::string(texPath.C_Str());
		b = b.substr(0, b.length() - 4);
		std::cout << "NORMAL: " << b << "\n";

		// Sponza/mesh materials: high roughness (matte), no metallic to avoid wet look
		CreateMaterial(scene->mMaterials[k]->GetName().C_Str(), k, TexOffsets[a], TexOffsets[b],
			XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
			XMFLOAT3(0.04f, 0.04f, 0.04f),
			0.82f,
			0.0f);
	}

	UINT totalMeshSize = 0;
	UINT k = vertices.size();
	std::vector<std::pair<GeometryGenerator::MeshData, SubmeshGeometry>>meshSubmeshes;
	for (auto mesh : meshDatas)
	{
		meshVertexOffset = meshVertexOffset + prevVertSize;
		prevVertSize = mesh.Vertices.size();
		totalMeshSize += mesh.Vertices.size();

		meshIndexOffset = meshIndexOffset + prevIndSize;
		prevIndSize = mesh.Indices32.size();
		SubmeshGeometry meshSubmesh;
		meshSubmesh.IndexCount = (UINT)mesh.Indices32.size();
		meshSubmesh.StartIndexLocation = meshIndexOffset;
		meshSubmesh.BaseVertexLocation = meshVertexOffset;
		GeometryGenerator::MeshData m = mesh;
		meshSubmeshes.push_back(std::make_pair(m, meshSubmesh));
	}
	/////////
	/////
	for (auto mesh : meshDatas)
	{
		for (size_t i = 0; i < mesh.Vertices.size(); ++i, ++k)
		{
			vertices.push_back(Vertex(mesh.Vertices[i].Position, mesh.Vertices[i].Normal, mesh.Vertices[i].TexC, mesh.Vertices[i].TangentU));
		}
	}
	////////

	///////
	for (auto mesh : meshDatas)
	{
		indices.insert(indices.end(), std::begin(mesh.GetIndices16()), std::end(mesh.GetIndices16()));
	}
	///////
	Geo->MultiDrawArgs[name] = meshSubmeshes;
}
void TexColumnsApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 15, 15);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.25f, 0.00f, 1.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();


	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));


	UINT meshVertexOffset = cylinderVertexOffset;
	UINT meshIndexOffset = cylinderIndexOffset;
	UINT prevIndSize = (UINT)cylinder.Indices32.size();
	UINT prevVertSize = (UINT)cylinder.Vertices.size();

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";
	BuildCustomMeshGeometry("sponza", meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, geo.get());
	BuildCustomMeshGeometry("negr", meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, geo.get());
	BuildCustomMeshGeometry("left", meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, geo.get());
	BuildCustomMeshGeometry("right", meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, geo.get());
	BuildCustomMeshGeometry("plane2", meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, geo.get());


	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);


	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void TexColumnsApp::BuildTerrainGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(1.0f, 1.0f, 64, 64);

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		const auto& gv = grid.Vertices[i];
		vertices[i].Pos = XMFLOAT3(gv.Position.x + 0.5f, gv.Position.y, gv.Position.z + 0.5f);
		vertices[i].Normal = gv.Normal;
		vertices[i].TexC = gv.TexC;
		vertices[i].Tangent = gv.TangentU;
	}

	std::vector<std::uint16_t> indices(grid.GetIndices16().begin(), grid.GetIndices16().end());

	SubmeshGeometry terrainSubmesh;
	terrainSubmesh.IndexCount = (UINT)indices.size();
	terrainSubmesh.StartIndexLocation = 0;
	terrainSubmesh.BaseVertexLocation = 0;

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "terrainGrid";
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;
	geo->DrawArgs["terrain"] = terrainSubmesh;
	mGeometries[geo->Name] = std::move(geo);
}

void TexColumnsApp::BuildPSOs()
{
	const DXGI_FORMAT SceneColorFormat = mBackBufferFormat; // <- set to the EXACT format of mSceneTexture and TAA history.

	// Helper for default init.
	auto DefaultPso = [&]()
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
			d.SampleMask = UINT_MAX;
			d.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			d.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			d.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			d.SampleDesc.Count = 1;
			d.SampleDesc.Quality = 0;
			d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			for (int i = 0; i < 8; ++i) d.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
			d.DSVFormat = DXGI_FORMAT_UNKNOWN;
			return d;
		};

	{
		auto pso = DefaultPso();
		pso.pRootSignature = mRootSignature.Get();
		pso.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		pso.VS = { (BYTE*)mShaders["standardVS"]->GetBufferPointer(), mShaders["standardVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["opaquePS"]->GetBufferPointer(),   mShaders["opaquePS"]->GetBufferSize() };
		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = mBackBufferFormat;
		pso.DSVFormat = mDepthStencilFormat;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["opaque"])));
	}

	// GBUFFER (4 MRT)
	{
		auto pso = DefaultPso();
		pso.pRootSignature = mRootSignature.Get();
		pso.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		pso.VS = { (BYTE*)mShaders["gbufferVS"]->GetBufferPointer(), mShaders["gbufferVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["gbufferPS"]->GetBufferPointer(), mShaders["gbufferPS"]->GetBufferSize() };

		pso.NumRenderTargets = 4;
		pso.RTVFormats[0] = albedoFormat;                   // R8G8B8A8_UNORM
		pso.RTVFormats[1] = normalFormat;                   // R16G16B16A16_FLOAT
		pso.RTVFormats[2] = positionFormat;                 // R32G32B32A32_FLOAT
		pso.RTVFormats[3] = DXGI_FORMAT_R16G16_FLOAT;       // velocity
		pso.DSVFormat = mDepthStencilFormat;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["gbuffer"])));
	}

	// TERRAIN (same MRT as gbuffer, heightmap in VS)
	{
		auto pso = DefaultPso();
		pso.pRootSignature = mRootSignature.Get();
		pso.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		pso.VS = { (BYTE*)mShaders["terrainVS"]->GetBufferPointer(), mShaders["terrainVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["terrainPS"]->GetBufferPointer(), mShaders["terrainPS"]->GetBufferSize() };
		pso.NumRenderTargets = 4;
		pso.RTVFormats[0] = albedoFormat;
		pso.RTVFormats[1] = normalFormat;
		pso.RTVFormats[2] = positionFormat;
		pso.RTVFormats[3] = DXGI_FORMAT_R16G16_FLOAT;
		pso.DSVFormat = mDepthStencilFormat;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["terrain"])));
	}
	// TERRAIN WIREFRAME (debug)
	{
		auto pso = DefaultPso();
		pso.pRootSignature = mRootSignature.Get();
		pso.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		pso.VS = { (BYTE*)mShaders["terrainVS"]->GetBufferPointer(), mShaders["terrainVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["terrainPS"]->GetBufferPointer(), mShaders["terrainPS"]->GetBufferSize() };
		pso.NumRenderTargets = 4;
		pso.RTVFormats[0] = albedoFormat;
		pso.RTVFormats[1] = normalFormat;
		pso.RTVFormats[2] = positionFormat;
		pso.RTVFormats[3] = DXGI_FORMAT_R16G16_FLOAT;
		pso.DSVFormat = mDepthStencilFormat;
		pso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["terrain_wireframe"])));
	}

	// SHADOW MAP
	{
		auto pso = DefaultPso();
		pso.pRootSignature = mShadowPassRootSignature.Get();
		pso.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		pso.VS = { (BYTE*)mShaders["shadowVS"]->GetBufferPointer(), mShaders["shadowVS"]->GetBufferSize() };

		pso.NumRenderTargets = 0;
		pso.DSVFormat = SHADOW_MAP_DSV_FORMAT;

		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["shadow_map"])));
	}

	// LIGHTING FULLSCREEN (additive)
	// Renders into mSceneTexture (SceneColorFormat)

	{
		auto pso = DefaultPso();
		pso.pRootSignature = mLightingRootSignature.Get();

		// Fullscreen triangle: no VB
		pso.InputLayout = { nullptr, 0 };

		pso.VS = { (BYTE*)mShaders["lightingQUADVS"]->GetBufferPointer(), mShaders["lightingQUADVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["lightingPS"]->GetBufferPointer(),     mShaders["lightingPS"]->GetBufferSize() };

		// Additive blending (accumulate lights)
		D3D12_RENDER_TARGET_BLEND_DESC rt = {};
		rt.BlendEnable = TRUE;
		rt.LogicOpEnable = FALSE;
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_ONE;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		pso.BlendState.RenderTarget[0] = rt;

		// Fullscreen: depth off
		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = FALSE;
		pso.DepthStencilState.StencilEnable = FALSE;

		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = SceneColorFormat;
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["lightingQUAD"])));
	}

	// LIGHTING SHAPES DEBUG (wireframe meshes, not fullscreen)
	// If you still draw debug volumes with DrawIndexedInstanced
	{
		auto pso = DefaultPso();
		pso.pRootSignature = mLightingRootSignature.Get();
		pso.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		pso.VS = { (BYTE*)mShaders["lightingVS"]->GetBufferPointer(),       mShaders["lightingVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["lightingPSDebug"]->GetBufferPointer(),  mShaders["lightingPSDebug"]->GetBufferSize() };

		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

		// Usually still depth-test, but no depth writes
		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = TRUE;
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

		// Debug shapes also render into scene
		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = SceneColorFormat;
		pso.DSVFormat = mDepthStencilFormat;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["lightingShapes"])));
	}

	// POSTPROCESS -> backbuffer
	{
		auto pso = DefaultPso();
		pso.pRootSignature = mPostProcessRootSignature.Get();
		pso.InputLayout = { nullptr, 0 };
		pso.VS = { (BYTE*)mShaders["postprocessVS"]->GetBufferPointer(), mShaders["postprocessVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["postprocessPS"]->GetBufferPointer(), mShaders["postprocessPS"]->GetBufferSize() };

		pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		pso.DepthStencilState.DepthEnable = FALSE;
		pso.DepthStencilState.StencilEnable = FALSE;

		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = mBackBufferFormat;
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["PostProcess"])));
	}

	// TAA RESOLVE -> history RT (SceneColorFormat)
	{
		if (mTaaRootSignature == nullptr)
			throw std::runtime_error("mTaaRootSignature is null before creating TAAResolve PSO");

		auto pso = DefaultPso();
		pso.pRootSignature = mTaaRootSignature.Get();
		pso.InputLayout = { nullptr, 0 };
		pso.VS = { (BYTE*)mShaders["taaResolveVS"]->GetBufferPointer(), mShaders["taaResolveVS"]->GetBufferSize() };
		pso.PS = { (BYTE*)mShaders["taaResolvePS"]->GetBufferPointer(), mShaders["taaResolvePS"]->GetBufferSize() };

		pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pso.DepthStencilState.DepthEnable = FALSE;
		pso.DepthStencilState.StencilEnable = FALSE;

		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = SceneColorFormat;   // MUST match mTaaHistory format
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSOs["TAAResolve"])));
	}
}


void TexColumnsApp::BuildFrameResources()
{
	FlushCommandQueue();
	mFrameResources.clear();
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		// +1 object slot for terrain tiles
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size() + 1, (UINT)mMaterials.size(), (UINT)mLights.size()));
	}
	mChromaticAberrationCB = std::make_unique<UploadBuffer<float>>(md3dDevice.Get(), 1, true);
	mTaaCB = std::make_unique<UploadBuffer<TAAConstants>>(md3dDevice.Get(), 1, true);
	mTaaReprojectCB = std::make_unique<UploadBuffer<TAAReprojectConstants>>(md3dDevice.Get(), 1, true);
	mAtmosphereCB = std::make_unique<UploadBuffer<AtmosphereConstants>>(md3dDevice.Get(), 1, true);
	mDxrShadowCB = std::make_unique<UploadBuffer<DxrShadowConstants>>(md3dDevice.Get(), gNumFrameResources, true);

	mCurrFrameResourceIndex = 0;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
	for (auto& ri : mAllRitems)
	{
		ri->NumFramesDirty = gNumFrameResources;
	}
	for (auto& kv : mMaterials)
	{
		kv.second->NumFramesDirty = gNumFrameResources;
	}
}

void TexColumnsApp::BuildMaterials()
{
	CreateMaterial("NiggaMat", 0, TexOffsets["textures/texture"], TexOffsets["textures/texture_nm"],
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.04f, 0.04f, 0.04f),
		0.82f,
		0.0f);
	CreateMaterial("eye", 0, TexOffsets["textures/eye"], TexOffsets["textures/eye_nm"],
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.05f, 0.05f, 0.05f),
		0.7f,
		0.0f);
	CreateMaterial("map", 0, TexOffsets["textures/HeightMap2"], TexOffsets["textures/HeightMap2"],
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.05f, 0.05f, 0.05f),
		0.3f,
		0.0f);
	CreateMaterial("map2", 0, TexOffsets["textures/HeightMap"], TexOffsets["textures/HeightMap"],
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.05f, 0.05f, 0.05f),
		0.3f,
		0.0f);

	// Debug material for mode 1 (moving objects).
	CreateMaterial("MovingRed", 0, TexOffsets["textures/white1x1"], TexOffsets["textures/default_nmap"],
		XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, 0.0f);

	// PBR test spheres (metallic/roughness grid)
	const int kPbrCols = 5; // metallic
	const int kPbrRows = 5; // roughness
	for (int r = 0; r < kPbrRows; ++r)
	{
		// Avoid 0 roughness (causes very sharp highlights / fireflies in LDR)
		const float roughness = std::clamp((float)r / (float)(kPbrRows - 1), 0.04f, 1.0f);
		for (int c = 0; c < kPbrCols; ++c)
		{
			const float metallic = std::clamp((float)c / (float)(kPbrCols - 1), 0.0f, 1.0f);

			const std::string matName =
				"PBR_Sphere_r" + std::to_string(r) +
				"_c" + std::to_string(c);

			CreateMaterial(matName, 0,
				TexOffsets["textures/white1x1"],
				TexOffsets["textures/default_nmap"],
				XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f),
				XMFLOAT3(0.04f, 0.04f, 0.04f),
				roughness,
				metallic);
		}
	}
	int terrainTex = (TexOffsets.find("001/Height_Out") != TexOffsets.end()) ? TexOffsets["001/Height_Out"] : TexOffsets["textures/HeightMap2"];
	CreateMaterial("TerrainMat", 0, terrainTex, terrainTex,
		XMFLOAT4(0.4f, 0.5f, 0.3f, 1.0f), XMFLOAT3(0.04f, 0.04f, 0.04f), 0.9f, 0.0f);
	mTerrainMaterialIndex = mMaterials["TerrainMat"]->MatCBIndex;
}
void TexColumnsApp::RenderCustomMesh(std::string unique_name, std::string meshname, std::string materialName, XMFLOAT3 Scale, XMFLOAT3 Rotation, XMFLOAT3 Position)
{
	for (int i = 0; i < ObjectsMeshCount[meshname]; i++)
	{
		auto rItem = std::make_unique<RenderItem>();
		std::string textureFile;
		rItem->Name = unique_name;
		auto trans = XMMatrixTranslation(Position.x, Position.y, Position.z);
		auto rot = XMMatrixRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z);
		auto scl = XMMatrixScaling(Scale.x, Scale.y, Scale.z);
		XMStoreFloat4x4(&rItem->TexTransform, XMMatrixScaling(1, 1., 1.));
		XMStoreFloat4x4(&rItem->World, scl * rot * trans);
		rItem->PrevWorld = rItem->World; // NEW
		rItem->TranslationM = trans;
		rItem->RotationM = rot;
		rItem->ScaleM = scl;

		rItem->Position = Position;
		rItem->RotationAngle = Rotation;
		rItem->Scale = Scale;
		rItem->ObjCBIndex = mAllRitems.size();
		rItem->Geo = mGeometries["shapeGeo"].get();
		rItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		std::string matname = rItem->Geo->MultiDrawArgs[meshname][i].first.matName;
		std::cout << " mat : " << matname << "\n";
		std::cout << unique_name << " " << matname << "\n";
		if (materialName != "") matname = materialName;
		rItem->Mat = mMaterials[matname].get();
		rItem->BaseMat = rItem->Mat;
		rItem->IndexCount = rItem->Geo->MultiDrawArgs[meshname][i].second.IndexCount;
		rItem->StartIndexLocation = rItem->Geo->MultiDrawArgs[meshname][i].second.StartIndexLocation;
		rItem->BaseVertexLocation = rItem->Geo->MultiDrawArgs[meshname][i].second.BaseVertexLocation;
		mAllRitems.push_back(std::move(rItem));
	}

}



void TexColumnsApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();
	boxRitem->Name = "box";
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 5.0f, -10.0f));
	boxRitem->PrevWorld = boxRitem->World;
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1, 1, 1));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Mat = mMaterials["NiggaMat"].get();
	boxRitem->BaseMat = boxRitem->Mat;
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

	// Объект для проверки RT-теней: куб перед сценой, отбрасывает тень на землю/спонзу
	auto shadowTestRitem = std::make_unique<RenderItem>();
	shadowTestRitem->Name = "shadowTestBox";
	XMStoreFloat4x4(&shadowTestRitem->World,
		XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(25.0f, 4.0f, 0.0f));
	shadowTestRitem->PrevWorld = shadowTestRitem->World;
	XMStoreFloat4x4(&shadowTestRitem->TexTransform, XMMatrixScaling(1, 1, 1));
	shadowTestRitem->ObjCBIndex = (UINT)mAllRitems.size();
	shadowTestRitem->Mat = mMaterials["NiggaMat"].get();
	shadowTestRitem->BaseMat = shadowTestRitem->Mat;
	shadowTestRitem->Geo = mGeometries["shapeGeo"].get();
	shadowTestRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	shadowTestRitem->IndexCount = shadowTestRitem->Geo->DrawArgs["box"].IndexCount;
	shadowTestRitem->StartIndexLocation = shadowTestRitem->Geo->DrawArgs["box"].StartIndexLocation;
	shadowTestRitem->BaseVertexLocation = shadowTestRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(shadowTestRitem));

	RenderCustomMesh("building", "sponza", "", XMFLOAT3(0.07, 0.07, 0.07), XMFLOAT3(0, 3.14 / 2, 0), XMFLOAT3(0, 0, 0));
	RenderCustomMesh("nigga", "negr", "NiggaMat", XMFLOAT3(3, 3, 3), XMFLOAT3(0, 3.14, 0), XMFLOAT3(6, 5, 0));
	RenderCustomMesh("nigga2", "negr", "NiggaMat", XMFLOAT3(3, 3, 3), XMFLOAT3(0, -3.14 / 2, 0), XMFLOAT3(-10, 7, 30));

	// PBR test spheres grid (5x5: metallic across X, roughness across Z)
	{
		const int kPbrCols = 5;
		const int kPbrRows = 5;
		const float spacing = 2.2f;
		const float startX = -0.5f * (kPbrCols - 1) * spacing;
		const float startZ = -0.5f * (kPbrRows - 1) * spacing;
		const float y = 120.0f; // above floor and shadows so reflections are visible

		for (int r = 0; r < kPbrRows; ++r)
		{
			for (int c = 0; c < kPbrCols; ++c)
			{
				const std::string matName =
					"PBR_Sphere_r" + std::to_string(r) +
					"_c" + std::to_string(c);

				auto sphereRitem = std::make_unique<RenderItem>();
				sphereRitem->Name = "pbrSphere";

				const float x = startX + c * spacing;
				const float z = startZ + r * spacing;

				sphereRitem->Position = { x, y, z };
				sphereRitem->RotationAngle = { 0.0f, 0.0f, 0.0f };
				sphereRitem->Scale = { 2.0f, 2.0f, 2.0f }; // sphere mesh radius is 0.5

				sphereRitem->TranslationM = XMMatrixTranslation(x, y, z);
				sphereRitem->RotationM = XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f);
				sphereRitem->ScaleM = XMMatrixScaling(2.0f, 2.0f, 2.0f);

				XMStoreFloat4x4(&sphereRitem->TexTransform, XMMatrixIdentity());
				XMStoreFloat4x4(&sphereRitem->World, sphereRitem->ScaleM * sphereRitem->RotationM * sphereRitem->TranslationM);
				sphereRitem->PrevWorld = sphereRitem->World;

				sphereRitem->ObjCBIndex = (UINT)mAllRitems.size();
				sphereRitem->Mat = mMaterials[matName].get();
				sphereRitem->BaseMat = sphereRitem->Mat;
				sphereRitem->Geo = mGeometries["shapeGeo"].get();
				sphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
				sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
				sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

				mAllRitems.push_back(std::move(sphereRitem));
			}
		}
	}

	BuildFrameResources();
	//RenderCustomMesh("plan", "plane2", "map", XMMatrixScaling(3, 3, 3), XMMatrixRotationRollPitchYaw(3.14, 0, 3.14), XMMatrixTranslation(0,-10,0));
	//RenderCustomMesh("plan", "plane2", "map2", XMMatrixScaling(3, 3, 3), XMMatrixRotationRollPitchYaw(3.14, 0, 3.14), XMMatrixTranslation(0,10,0));
	// All the render items are opaque.
	for (auto& e : mAllRitems)
	{
		if (e->Name == "plan")
		{
			XMStoreFloat4x4(&e->TexTransform, XMMatrixScaling(1, 1, 1));
		}
		mOpaqueRitems.push_back(e.get());
	}
}


// NOT USING
void TexColumnsApp::Draw(const GameTimer& gt)
{

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());


	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);


	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(1, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}
void TexColumnsApp::DrawSceneToShadowMap()
{


	UINT shadowCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassShadowConstants));
	for (auto light : mLights)
	{
		if (light.type == 2 || light.type == 3)
		{
			if (light.CastsShadows)
			{
				mCommandList->SetPipelineState(mPSOs["shadow_map"].Get());
				mCommandList->SetGraphicsRootSignature(mShadowPassRootSignature.Get());
				// Set the viewport and scissor rect for the shadow map.
				mCommandList->RSSetViewports(1, &mShadowViewport);
				mCommandList->RSSetScissorRects(1, &mShadowScissorRect);
				// Transition the shadow map from generic read to depth-write.
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(light.ShadowMap.Get(),
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));

				// Clear the shadow map.
				mCommandList->ClearDepthStencilView(light.ShadowMapDsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

				// Set the shadow map as the depth-stencil buffer. No render targets.
				mCommandList->OMSetRenderTargets(0, nullptr, FALSE, &light.ShadowMapDsvHandle);

				D3D12_GPU_VIRTUAL_ADDRESS shadowCBAddress = mCurrFrameResource->PassShadowCB->Resource()->GetGPUVirtualAddress() + light.LightCBIndex * shadowCBByteSize;
				mCommandList->SetGraphicsRootConstantBufferView(1, shadowCBAddress);
				// Draw all opaque items.
				UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

				auto objectCB = mCurrFrameResource->ObjectCB->Resource();

				// For each render item...
				for (size_t i = 0; i < mOpaqueRitems.size(); ++i)
				{
					auto ri = mOpaqueRitems[i];
					mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
					mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
					mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

					D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
					mCommandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

					mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
				}
				// Transition the shadow map from depth-write to pixel shader resource for the lighting pass.
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(light.ShadowMap.Get(),
					D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			}

		}

	}

}

void TexColumnsApp::DeferredDraw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

	DrawSceneToShadowMap();

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	ID3D12DescriptorHeap* heaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

	auto passCB = mCurrFrameResource->PassCB->Resource();


	// 1) GEOMETRY PASS (GBuffer)
	Transition(mGBufferAlbedo.Get(), mGBufferState[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
	Transition(mGBufferNormal.Get(), mGBufferState[1], D3D12_RESOURCE_STATE_RENDER_TARGET);
	Transition(mGBufferPosition.Get(), mGBufferState[2], D3D12_RESOURCE_STATE_RENDER_TARGET);
	Transition(mGBufferVelocity.Get(), mGBufferState[3], D3D12_RESOURCE_STATE_RENDER_TARGET);

	// Depth = DEPTH_WRITE
	Transition(mDepthStencilBuffer.Get(), mDepthState, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	mCommandList->SetPipelineState(mPSOs["gbuffer"].Get());

	CD3DX12_CPU_DESCRIPTOR_HANDLE gbufferRtvs[4] =
	{
		mGBufferRTVs[0],
		mGBufferRTVs[1],
		mGBufferRTVs[2],
		mGBufferRTVs[3]
	};

	// Clear GBuffer (должно совпадать с clearValue при создании = Black)
	for (int i = 0; i < 3; ++i)
		mCommandList->ClearRenderTargetView(gbufferRtvs[i], Colors::Black, 0, nullptr);


	const float velClear[4] = { 0,0,0,0 };
	mCommandList->ClearRenderTargetView(gbufferRtvs[3], velClear, 0, nullptr);

	mCommandList->ClearDepthStencilView(
		DepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(4, gbufferRtvs, TRUE, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());

	DrawTerrain(mCommandList.Get());
	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// GBuffer -> SRV для lighting
	const D3D12_RESOURCE_STATES kSrvRead =
		(D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Transition(mGBufferAlbedo.Get(), mGBufferState[0], kSrvRead);
	Transition(mGBufferNormal.Get(), mGBufferState[1], kSrvRead);
	Transition(mGBufferPosition.Get(), mGBufferState[2], kSrvRead);
	Transition(mGBufferVelocity.Get(), mGBufferState[3], kSrvRead);

	// DXR shadow mask pass (RayQuery compute) before lighting
	DispatchDxrShadowMask(mCommandList.Get());

	// 2) LIGHTING PASS -> mSceneTexture
	Transition(mSceneTexture.Get(), mSceneState, D3D12_RESOURCE_STATE_RENDER_TARGET);

	mCommandList->OMSetRenderTargets(1, &mSceneRtvHandle, TRUE, nullptr);
	mCommandList->ClearRenderTargetView(mSceneRtvHandle, Colors::Black, 0, nullptr);

	mCommandList->SetGraphicsRootSignature(mLightingRootSignature.Get());
	mCommandList->SetPipelineState(mPSOs["lightingQUAD"].Get());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// SRVs (t0=pos, t1=nrm, t2=alb)
	CD3DX12_GPU_DESCRIPTOR_HANDLE posH(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	posH.Offset(mGBufferSrvIndexPosition, mCbvSrvDescriptorSize);

	CD3DX12_GPU_DESCRIPTOR_HANDLE nrmH(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	nrmH.Offset(mGBufferSrvIndexNormal, mCbvSrvDescriptorSize);

	CD3DX12_GPU_DESCRIPTOR_HANDLE albH(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	albH.Offset(mGBufferSrvIndexAlbedo, mCbvSrvDescriptorSize);

	CD3DX12_GPU_DESCRIPTOR_HANDLE velH(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	velH.Offset(mGBufferSrvIndexVelocity, mCbvSrvDescriptorSize);

	mCommandList->SetGraphicsRootDescriptorTable(0, posH);
	mCommandList->SetGraphicsRootDescriptorTable(1, nrmH);
	mCommandList->SetGraphicsRootDescriptorTable(2, albH);
	mCommandList->SetGraphicsRootDescriptorTable(8, velH);

	auto getTexOffset = [&](const std::string& a, const std::string& b, const std::string& fallback) -> int
		{
			auto it = TexOffsets.find(a);
			if (it != TexOffsets.end()) return it->second;
			it = TexOffsets.find(b);
			if (it != TexOffsets.end()) return it->second;
			it = TexOffsets.find(fallback);
			if (it != TexOffsets.end()) return it->second;
			return -1;
		};

	const int irrIdx = getTexOffset("irradiance", "textures/irradiance", "textures/sunsetcube1024");
	const int preIdx = getTexOffset("prefiltered", "textures/prefiltered", "textures/sunsetcube1024");
	const int brdfIdx = getTexOffset("brdfLUT", "textures/brdfLUT", "textures/white1x1");
	const int skyIdx = getTexOffset("skybox", "textures/skybox", "textures/sunsetcube1024");

	if (irrIdx >= 0)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		h.Offset(irrIdx, mCbvSrvDescriptorSize);
		mCommandList->SetGraphicsRootDescriptorTable(10, h);
	}
	if (preIdx >= 0)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		h.Offset(preIdx, mCbvSrvDescriptorSize);
		mCommandList->SetGraphicsRootDescriptorTable(11, h);
	}
	if (brdfIdx >= 0)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		h.Offset(brdfIdx, mCbvSrvDescriptorSize);
		mCommandList->SetGraphicsRootDescriptorTable(12, h);
	}
	if (skyIdx >= 0)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		h.Offset(skyIdx, mCbvSrvDescriptorSize);
		mCommandList->SetGraphicsRootDescriptorTable(13, h);
	}

	// b3 debug constants are set per-light (so DXR mask is used only for one selected light).

	// b4 atmosphere (sun + rayleigh/mie/turbidity)
	if (mAtmosphereCB != nullptr)
		mCommandList->SetGraphicsRootConstantBufferView(14, mAtmosphereCB->Resource()->GetGPUVirtualAddress());

	// b0 pass
	mCommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());

	UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(LightConstants));
	auto lightCB = mCurrFrameResource->LightCB->Resource();

	for (auto& light : mLights)
	{
		const bool useDxrThisLight =
			(mEnableDxrShadows && mDxrShadowMask && mDxrTlas && mDxrShadowMaskSrvIndex >= 0) &&
			(light.CastsShadows) &&
			(light.type == 2) && // directional only (one mask per frame)
			(light.LightCBIndex == mDxrShadowLightCBIndex);

		UINT debugConsts[4] =
		{
			(UINT)gVelocityDebugMode,
			(UINT)(useDxrThisLight ? 1 : 0),
			(UINT)((useDxrThisLight && mVisualizeDxrShadowMask) ? 1 : 0),
			0u
		};
		mCommandList->SetGraphicsRoot32BitConstants(9, 4, debugConsts, 0);

		// b2 light
		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress =
			lightCB->GetGPUVirtualAddress() + light.LightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(5, lightCBAddress);

		if (light.CastsShadows)
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrv(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			shadowSrv.Offset((INT)light.ShadowMapSrvHeapIndex, mCbvSrvDescriptorSize);

			CD3DX12_GPU_DESCRIPTOR_HANDLE patternSrv(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			if (useDxrThisLight)
				patternSrv.Offset((INT)mDxrShadowMaskSrvIndex, mCbvSrvDescriptorSize); // t4 = DXR shadow mask
			else
				patternSrv.Offset((INT)TexOffsets["textures/pattern"], mCbvSrvDescriptorSize); // t4 = pattern

			mCommandList->SetGraphicsRootDescriptorTable(6, shadowSrv);
			mCommandList->SetGraphicsRootDescriptorTable(7, patternSrv);
		}

		// fullscreen add for every light
		mCommandList->DrawInstanced(3, 1, 0, 0);
	}

	Transition(mSceneTexture.Get(), mSceneState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// 3) TAA (resolve) + depth history copy
	bool taaReady =
		(mPSOs.find("TAAResolve") != mPSOs.end()) &&
		(mPSOs["TAAResolve"] != nullptr) &&
		(mTaaRootSignature != nullptr) &&
		(mTaaCB != nullptr) &&
		(mTaaReprojectCB != nullptr) &&
		(mTaaHistory[0] != nullptr) && (mTaaHistory[1] != nullptr) &&
		(mTaaDepthHistory[0] != nullptr) && (mTaaDepthHistory[1] != nullptr);

	UINT readIdx = mTaaHistoryIndex;
	UINT writeIdx = 1 - mTaaHistoryIndex;

	if (taaReady)
	{
		//copy depth -> depthHistory[write]
		Transition(mDepthStencilBuffer.Get(), mDepthState, D3D12_RESOURCE_STATE_COPY_SOURCE);

		Transition(mTaaDepthHistory[writeIdx].Get(), mTaaDepthHistState[writeIdx], D3D12_RESOURCE_STATE_COPY_DEST);

		mCommandList->CopyResource(mTaaDepthHistory[writeIdx].Get(), mDepthStencilBuffer.Get());

		Transition(mTaaDepthHistory[writeIdx].Get(), mTaaDepthHistState[writeIdx], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		Transition(mDepthStencilBuffer.Get(), mDepthState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		// resolve history color -> history[write]
		Transition(mTaaHistory[writeIdx].Get(), mTaaHistState[writeIdx], D3D12_RESOURCE_STATE_RENDER_TARGET);

		Transition(mTaaHistory[readIdx].Get(), mTaaHistState[readIdx], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		Transition(mTaaDepthHistory[readIdx].Get(), mTaaDepthHistState[readIdx], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		Transition(mSceneTexture.Get(), mSceneState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		mCommandList->SetPipelineState(mPSOs["TAAResolve"].Get());
		mCommandList->SetGraphicsRootSignature(mTaaRootSignature.Get());
		mCommandList->OMSetRenderTargets(1, &mTaaHistoryRtv[writeIdx], TRUE, nullptr);

		mCommandList->SetGraphicsRootDescriptorTable(0, mTaaSrvTableBase[readIdx]);
		mCommandList->SetGraphicsRootConstantBufferView(1, mTaaCB->Resource()->GetGPUVirtualAddress());
		mCommandList->SetGraphicsRootConstantBufferView(2, mTaaReprojectCB->Resource()->GetGPUVirtualAddress());

		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mCommandList->DrawInstanced(3, 1, 0, 0);

		Transition(mTaaHistory[writeIdx].Get(), mTaaHistState[writeIdx], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		// вернуть depth в DEPTH_WRITE для следующего кадра
		Transition(mDepthStencilBuffer.Get(), mDepthState, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		mTaaHistoryIndex = writeIdx;
		mTaaHistoryValid = true;
	}
	else
	{
		// на всякий: вернуть depth
		Transition(mDepthStencilBuffer.Get(), mDepthState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}

	// 4) POST -> Backbuffer
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	mCommandList->SetPipelineState(mPSOs["PostProcess"].Get());
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), TRUE, nullptr);

	mCommandList->SetGraphicsRootSignature(mPostProcessRootSignature.Get());

	// input: taa history if valid, else scene
	if (taaReady && mTaaHistoryValid)
		mCommandList->SetGraphicsRootDescriptorTable(0, mTaaHistorySrv[mTaaHistoryIndex]);
	else
		mCommandList->SetGraphicsRootDescriptorTable(0, mSceneSrvHandle);

	mCommandList->SetGraphicsRootConstantBufferView(1, mChromaticAberrationCB->Resource()->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE luthandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	if (CCenabled)
		luthandle.Offset((INT)TexOffsets["textures/lut_effect"], mCbvSrvDescriptorSize);
	else
		luthandle.Offset((INT)TexOffsets["textures/lut_neutral"], mCbvSrvDescriptorSize);

	mCommandList->SetGraphicsRootDescriptorTable(2, luthandle);

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->DrawInstanced(3, 1, 0, 0);

	// ImGui
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	{
		HRESULT hr = mSwapChain->Present(0, 0);
		if (FAILED(hr))
		{
			HRESULT dr = (md3dDevice ? md3dDevice->GetDeviceRemovedReason() : E_FAIL);
			std::string msg = "Present failed. hr=" + d3dUtil::ToString(hr) +
				" removedReason=" + d3dUtil::ToString(dr) + "\n";
			OutputDebugStringA(msg.c_str());
			ThrowIfFailed(hr);
		}
	}
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}







void TexColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		diffuseHandle.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, diffuseHandle);
		CD3DX12_GPU_DESCRIPTOR_HANDLE normalHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		normalHandle.Offset(ri->Mat->NormalSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(1, normalHandle);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(2, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(4, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void TexColumnsApp::DrawTerrain(ID3D12GraphicsCommandList* cmdList)
{
	if (!mTerrainEnabled || !mTerrain || mTerrain->GetVisibleTiles().empty()) return;
	auto* geo = mGeometries["terrainGrid"].get();
	if (!geo) return;
	const auto& drawArg = geo->DrawArgs["terrain"];
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
	const UINT terrainObjCBIndex = (UINT)mAllRitems.size();
	auto objectCB = mCurrFrameResource->ObjectCB.get();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();
	Material* terrainMat = mMaterials["TerrainMat"].get();
	if (!terrainMat || mTerrainMaterialIndex < 0) return;

	auto psoName = mTerrainWireframe ? "terrain_wireframe" : "terrain";
	auto it = mPSOs.find(psoName);
	if (it == mPSOs.end()) it = mPSOs.find("terrain");
	if (it == mPSOs.end() || !it->second) return;
	cmdList->SetPipelineState(it->second.Get());
	cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
	cmdList->IASetIndexBuffer(&geo->IndexBufferView());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (const TerrainTile& tile : mTerrain->GetVisibleTiles())
	{
		int srvIndex = (tile.HeightmapSrvIndex >= 0) ? tile.HeightmapSrvIndex : mTerrainFallbackHeightmapIndex;
		if (srvIndex < 0) continue;
		ObjectConstants objConstants;
		objConstants.World = tile.World;
		objConstants.PrevWorld = tile.PrevWorld;
		objConstants.TexTransform = MathHelper::Identity4x4();
		XMMATRIX world = XMLoadFloat4x4(&tile.World);
		XMVECTOR det;
		XMStoreFloat4x4(&objConstants.InvWorld, XMMatrixInverse(&det, world));
		objectCB->CopyData(terrainObjCBIndex, objConstants);

		CD3DX12_GPU_DESCRIPTOR_HANDLE heightmapHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		heightmapHandle.Offset(srvIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, heightmapHandle);
		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		diffuseHandle.Offset(terrainMat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(1, diffuseHandle);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = mCurrFrameResource->ObjectCB->Resource()->GetGPUVirtualAddress() + terrainObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + mTerrainMaterialIndex * matCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(2, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(4, matCBAddress);

		cmdList->DrawIndexedInstanced(drawArg.IndexCount, 1, drawArg.StartIndexLocation, drawArg.BaseVertexLocation, 0);
	}
}

void TexColumnsApp::CreateDxrShadowMaskResources()
{
	if (!mEnableDxrShadows) return;

	// Create (or recreate) shadow mask texture (R16_FLOAT UAV/SRV).
	mDxrShadowMask.Reset();

	const UINT s = (UINT)((mDxrShadowDownscale <= 1) ? 1 : (mDxrShadowDownscale <= 2) ? 2 : 4);
	const UINT maskW = (mClientWidth + s - 1) / s;
	const UINT maskH = (mClientHeight + s - 1) / s;

	D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16_FLOAT,
		(UINT64)maskW,
		(UINT)maskH,
		1, 1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mDxrShadowMask)));

	mDxrShadowMaskState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	mDxrShadowMask->SetName(L"DXR Shadow Mask");
}

void TexColumnsApp::CreateDxrShadowDescriptors()
{
	if (!mEnableDxrShadows || mSrvDescriptorHeap == nullptr) return;
	if (mDxrTlasSrvIndex < 0 || mDxrShadowMaskUavIndex < 0 || mDxrShadowMaskSrvIndex < 0) return;

	auto cpuAt = [&](int idx)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
			h.Offset(idx, mCbvSrvDescriptorSize);
			return h;
		};

	// TLAS SRV (special: pass nullptr resource)
	if (mDxrTlas)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc = {};
		tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		tlasDesc.RaytracingAccelerationStructure.Location = mDxrTlas->GetGPUVirtualAddress();
		md3dDevice->CreateShaderResourceView(nullptr, &tlasDesc, cpuAt(mDxrTlasSrvIndex));
	}

	// Shadow mask UAV + SRV
	if (mDxrShadowMask)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
		uav.Format = DXGI_FORMAT_R16_FLOAT;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav.Texture2D.MipSlice = 0;
		md3dDevice->CreateUnorderedAccessView(mDxrShadowMask.Get(), nullptr, &uav, cpuAt(mDxrShadowMaskUavIndex));

		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Format = DXGI_FORMAT_R16_FLOAT;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MostDetailedMip = 0;
		srv.Texture2D.MipLevels = 1;
		srv.Texture2D.ResourceMinLODClamp = 0.0f;
		md3dDevice->CreateShaderResourceView(mDxrShadowMask.Get(), &srv, cpuAt(mDxrShadowMaskSrvIndex));
	}
}

void TexColumnsApp::BuildDxrAccelerationStructures()
{
	if (!mEnableDxrShadows) return;
	try
	{
		if (mOpaqueRitems.empty()) return;

		ComPtr<ID3D12Device5> device5;
		if (FAILED(md3dDevice.As(&device5)))
		{
			mEnableDxrShadows = false;
			return;
		}
		ComPtr<ID3D12GraphicsCommandList4> cmd4;
		ThrowIfFailed(mCommandList.As(&cmd4));

	// Cache BLAS per unique (Geo + submesh range).
	struct BlasKey
	{
		MeshGeometry* Geo = nullptr;
		UINT IndexCount = 0;
		UINT StartIndexLocation = 0;
		INT BaseVertexLocation = 0;
	};
	struct BlasKeyHash
	{
		size_t operator()(const BlasKey& k) const noexcept
		{
			size_t h = std::hash<void*>()(k.Geo);
			auto hc = [&](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
			hc(std::hash<UINT>()(k.IndexCount));
			hc(std::hash<UINT>()(k.StartIndexLocation));
			hc(std::hash<int>()(k.BaseVertexLocation));
			return h;
		}
	};
	struct BlasKeyEq
	{
		bool operator()(const BlasKey& a, const BlasKey& b) const noexcept
		{
			return a.Geo == b.Geo &&
				a.IndexCount == b.IndexCount &&
				a.StartIndexLocation == b.StartIndexLocation &&
				a.BaseVertexLocation == b.BaseVertexLocation;
		}
	};

	std::unordered_map<BlasKey, UINT, BlasKeyHash, BlasKeyEq> blasIndex;
	struct BlasBuild
	{
		BlasKey Key;
		D3D12_RAYTRACING_GEOMETRY_DESC Geom = {};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Inputs = {};
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO Info = {};
	};
	std::vector<BlasBuild> builds;
	builds.reserve(mOpaqueRitems.size());

	UINT64 maxScratch = 0;
	for (auto ri : mOpaqueRitems)
	{
		if (!ri || !ri->Geo || !ri->Geo->VertexBufferGPU || !ri->Geo->IndexBufferGPU)
			continue;

		BlasKey key;
		key.Geo = ri->Geo;
		key.IndexCount = ri->IndexCount;
		key.StartIndexLocation = ri->StartIndexLocation;
		key.BaseVertexLocation = ri->BaseVertexLocation;

		if (blasIndex.find(key) != blasIndex.end())
			continue;

		BlasBuild b;
		b.Key = key;

		const UINT stride = sizeof(Vertex);
		const UINT totalVerts = (UINT)(key.Geo->VertexBufferByteSize / stride);
		const UINT vertsFromBase = (totalVerts > (UINT)max(0, key.BaseVertexLocation)) ? (totalVerts - (UINT)key.BaseVertexLocation) : totalVerts;

		b.Geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		b.Geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		b.Geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		b.Geom.Triangles.VertexCount = vertsFromBase;
		b.Geom.Triangles.VertexBuffer.StartAddress =
			key.Geo->VertexBufferGPU->GetGPUVirtualAddress() + (UINT64)key.BaseVertexLocation * stride;
		b.Geom.Triangles.VertexBuffer.StrideInBytes = stride;

		const DXGI_FORMAT idxFmt = key.Geo->IndexFormat;
		const UINT idxStride = (idxFmt == DXGI_FORMAT_R32_UINT) ? 4u : 2u;
		b.Geom.Triangles.IndexFormat = idxFmt;
		b.Geom.Triangles.IndexCount = key.IndexCount;
		b.Geom.Triangles.IndexBuffer =
			key.Geo->IndexBufferGPU->GetGPUVirtualAddress() + (UINT64)key.StartIndexLocation * idxStride;
		b.Geom.Triangles.Transform3x4 = 0;

		b.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		b.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		b.Inputs.NumDescs = 1;
		b.Inputs.pGeometryDescs = &b.Geom;
		b.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

		device5->GetRaytracingAccelerationStructurePrebuildInfo(&b.Inputs, &b.Info);
		maxScratch = max(maxScratch, b.Info.ScratchDataSizeInBytes);

		UINT idx = (UINT)builds.size();
		blasIndex[key] = idx;
		builds.push_back(std::move(b));
		// Fix pointer after move: Inputs.pGeometryDescs must point to the element's own Geom.
		builds[idx].Inputs.pGeometryDescs = &builds[idx].Geom;
	}

	if (builds.empty())
	{
		mEnableDxrShadows = false;
		return;
	}

	// Scratch for BLAS builds (reused).
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(maxScratch, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mDxrBlasScratch)));

	mDxrBlas.clear();
	mDxrBlas.resize(builds.size());

	for (UINT i = 0; i < (UINT)builds.size(); ++i)
	{
		UINT64 resultSize = builds[i].Info.ResultDataMaxSizeInBytes;
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr,
			IID_PPV_ARGS(&mDxrBlas[i])));

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
		desc.Inputs = builds[i].Inputs;
		desc.DestAccelerationStructureData = mDxrBlas[i]->GetGPUVirtualAddress();
		desc.ScratchAccelerationStructureData = mDxrBlasScratch->GetGPUVirtualAddress();
		cmd4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(mDxrBlas[i].Get()));
	}

	// TLAS instances (one per opaque render item, stable order).
	mDxrInstances.clear();
	mDxrInstances.reserve(mOpaqueRitems.size());
	for (auto ri : mOpaqueRitems)
	{
		if (!ri || !ri->Geo) continue;

		BlasKey key;
		key.Geo = ri->Geo;
		key.IndexCount = ri->IndexCount;
		key.StartIndexLocation = ri->StartIndexLocation;
		key.BaseVertexLocation = ri->BaseVertexLocation;

		auto it = blasIndex.find(key);
		if (it == blasIndex.end()) continue;

		mDxrInstances.push_back({ ri, it->second });
	}

	const UINT instanceCount = (UINT)mDxrInstances.size();
	if (instanceCount == 0)
	{
		mEnableDxrShadows = false;
		return;
	}

	const UINT64 instanceBufferSize = (UINT64)instanceCount * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

	// Per-frame instance buffers to avoid CPU/GPU races when updating transforms.
	mDxrInstanceDescs.clear();
	mDxrInstanceDescs.resize((size_t)gNumFrameResources);
	for (int fi = 0; fi < gNumFrameResources; ++fi)
	{
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(instanceBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mDxrInstanceDescs[fi])));

		D3D12_RAYTRACING_INSTANCE_DESC* mapped = nullptr;
		ThrowIfFailed(mDxrInstanceDescs[fi]->Map(0, nullptr, (void**)&mapped));

		for (UINT i = 0; i < instanceCount; ++i)
		{
			RenderItem* ri = mDxrInstances[i].Ri;
			const UINT blasIdx = mDxrInstances[i].BlasIndex;
			D3D12_RAYTRACING_INSTANCE_DESC& inst = mapped[i];
			memset(&inst, 0, sizeof(inst));
			inst.InstanceID = i;
			inst.InstanceContributionToHitGroupIndex = 0;
			inst.InstanceMask = 0xFF;
			// Disable triangle culling to avoid missing occluders due to winding/one-sided meshes.
			inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;

			// DXR instance transform is a row-major 3x4 that expects translation in the last column.
			// Our RenderItem::World is used with mul(pos, gWorld) in HLSL (row-vector convention),
			// so translation lives in the last ROW. Transpose to match DXR's 3x4 layout.
			XMMATRIX Wm = XMLoadFloat4x4(&ri->World);
			XMMATRIX Wt = XMMatrixTranspose(Wm);
			XMFLOAT4X4 Wtx;
			XMStoreFloat4x4(&Wtx, Wt);
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 4; ++c)
					inst.Transform[r][c] = Wtx.m[r][c];

			inst.AccelerationStructure = mDxrBlas[blasIdx]->GetGPUVirtualAddress();
		}
		mDxrInstanceDescs[fi]->Unmap(0, nullptr);
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs = {};
	tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	tlasInputs.NumDescs = instanceCount;
	tlasInputs.InstanceDescs = mDxrInstanceDescs.empty() ? 0 : mDxrInstanceDescs[0]->GetGPUVirtualAddress();
	tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasInfo = {};
	device5->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasInfo);

	const UINT64 tlasScratchSize = max(tlasInfo.ScratchDataSizeInBytes, tlasInfo.UpdateScratchDataSizeInBytes);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(tlasScratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mDxrTlasScratch)));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(tlasInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nullptr,
		IID_PPV_ARGS(&mDxrTlas)));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc = {};
	tlasDesc.Inputs = tlasInputs;
	tlasDesc.DestAccelerationStructureData = mDxrTlas->GetGPUVirtualAddress();
	tlasDesc.ScratchAccelerationStructureData = mDxrTlasScratch->GetGPUVirtualAddress();
	cmd4->BuildRaytracingAccelerationStructure(&tlasDesc, 0, nullptr);
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(mDxrTlas.Get()));
	}
	catch (const DxException&)
	{
		// Fail gracefully: keep app running with shadow maps.
		mEnableDxrShadows = false;
		mDxrBlasScratch.Reset();
		mDxrTlasScratch.Reset();
		mDxrTlas.Reset();
		mDxrInstanceDescs.clear();
		mDxrBlas.clear();
		mDxrInstances.clear();
	}
}

void TexColumnsApp::DispatchDxrShadowMask(ID3D12GraphicsCommandList* cmdList)
{
	if (!mEnableDxrShadows) return;
	if (!mDxrShadowRootSignature || !mDxrShadowPSO || !mDxrShadowMask || !mDxrTlas || !mDxrShadowCB) return;
	if (!cmdList) return;

	// Update TLAS every frame to match animated/moving render items.
	if (mDxrUpdateTlasEveryFrame && !mDxrInstances.empty() && !mDxrInstanceDescs.empty() && mDxrTlasScratch && mDxrTlas)
	{
		ComPtr<ID3D12GraphicsCommandList4> cmd4;
		if (SUCCEEDED(cmdList->QueryInterface(IID_PPV_ARGS(&cmd4))))
		{
			int fi = std::clamp(mCurrFrameResourceIndex, 0, gNumFrameResources - 1);
			auto& instBuf = mDxrInstanceDescs[(size_t)fi];
			if (!instBuf) return;

			D3D12_RAYTRACING_INSTANCE_DESC* mapped = nullptr;
			if (SUCCEEDED(instBuf->Map(0, nullptr, (void**)&mapped)))
			{
				for (UINT i = 0; i < (UINT)mDxrInstances.size(); ++i)
				{
					RenderItem* ri = mDxrInstances[i].Ri;
					const UINT blasIdx = mDxrInstances[i].BlasIndex;
					D3D12_RAYTRACING_INSTANCE_DESC& inst = mapped[i];

					// Update transform only (transpose for DXR 3x4 layout, see build path above).
					XMMATRIX Wm = XMLoadFloat4x4(&ri->World);
					XMMATRIX Wt = XMMatrixTranspose(Wm);
					XMFLOAT4X4 Wtx;
					XMStoreFloat4x4(&Wtx, Wt);
					for (int r = 0; r < 3; ++r)
						for (int c = 0; c < 4; ++c)
							inst.Transform[r][c] = Wtx.m[r][c];

					inst.AccelerationStructure = mDxrBlas[blasIdx]->GetGPUVirtualAddress();
				}
				instBuf->Unmap(0, nullptr);
			}

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
			inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			inputs.NumDescs = (UINT)mDxrInstances.size();
			inputs.InstanceDescs = instBuf->GetGPUVirtualAddress();
			// Update must use the same "preference" flags as the initial build (except ALLOW_UPDATE -> PERFORM_UPDATE).
			// Initial TLAS build uses PREFER_FAST_TRACE | ALLOW_UPDATE, so the update uses PREFER_FAST_TRACE | PERFORM_UPDATE.
			inputs.Flags =
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
			desc.Inputs = inputs;
			desc.SourceAccelerationStructureData = mDxrTlas->GetGPUVirtualAddress();
			desc.DestAccelerationStructureData = mDxrTlas->GetGPUVirtualAddress();
			desc.ScratchAccelerationStructureData = mDxrTlasScratch->GetGPUVirtualAddress();
			cmd4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
			cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(mDxrTlas.Get()));
		}
	}

	// Ensure output is UAV.
	Transition(mDxrShadowMask.Get(), mDxrShadowMaskState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	cmdList->SetComputeRootSignature(mDxrShadowRootSignature.Get());
	cmdList->SetPipelineState(mDxrShadowPSO.Get());

	// Descriptor heap already set in DeferredDraw (mSrvDescriptorHeap).
	auto gpuAt = [&](int idx)
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			h.Offset(idx, mCbvSrvDescriptorSize);
			return h;
		};

	// t0 = TLAS
	cmdList->SetComputeRootDescriptorTable(0, gpuAt(mDxrTlasSrvIndex));
	// t1 = Position
	cmdList->SetComputeRootDescriptorTable(1, gpuAt(mGBufferSrvIndexPosition));
	// t2 = Normal
	cmdList->SetComputeRootDescriptorTable(2, gpuAt(mGBufferSrvIndexNormal));
	// u0 = Shadow mask UAV
	cmdList->SetComputeRootDescriptorTable(3, gpuAt(mDxrShadowMaskUavIndex));

	// b0 = constants (per-frame)
	UINT cbByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(DxrShadowConstants));
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = mDxrShadowCB->Resource()->GetGPUVirtualAddress() + (UINT64)mCurrFrameResourceIndex * cbByteSize;
	cmdList->SetComputeRootConstantBufferView(4, cbAddr);

	auto md = mDxrShadowMask->GetDesc();
	const UINT maskW = (UINT)md.Width;
	const UINT maskH = (UINT)md.Height;
	const UINT gx = (maskW + 7) / 8;
	const UINT gy = (maskH + 7) / 8;
	cmdList->Dispatch(gx, gy, 1);

	// Make readable for lighting (t4 SRV).
	Transition(mDxrShadowMask.Get(), mDxrShadowMaskState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> TexColumnsApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
		6, // shaderRegister s6 (assuming 0-5 are used)
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // Comparison filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // Use BORDER for shadow maps
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.0f, // mipLODBias
		16,   // maxAnisotropy (not really used for comparison filter but set it)
		D3D12_COMPARISON_FUNC_LESS_EQUAL, // Comparison function
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK); // Border color (or opaque white if depth is 1.0)

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp,
		shadowSampler // Add the new sampler
	};
}

void TexColumnsApp::CreateTaaHistoryTextures()
{
	for (int i = 0; i < 2; ++i)
		mTaaHistory[i].Reset();

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = mClientWidth;
	texDesc.Height = mClientHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mBackBufferFormat; // DXGI_FORMAT_R8G8B8A8_UNORM 
	texDesc.SampleDesc.Count = 1;       // без MSAA
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = mBackBufferFormat;
	memcpy(clearValue.Color, Colors::Black, sizeof(float) * 4);

	for (int i = 0; i < 2; ++i)
	{
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 
			&clearValue,
			IID_PPV_ARGS(&mTaaHistory[i])
		));

		std::wstring name = L"TAA History ";
		name += (i == 0) ? L"0" : L"1";
		mTaaHistory[i]->SetName(name.c_str());

		mTaaHistState[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	}

	mTaaHistoryIndex = 0;
}

void TexColumnsApp::CreateTaaHistoryRtvs()
{
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = mBackBufferFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	UINT history0Index = SwapChainBufferCount + 5;
	UINT history1Index = SwapChainBufferCount + 6;

	mTaaHistoryRtv[0] = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), history0Index, mRtvDescriptorSize);
	mTaaHistoryRtv[1] = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), history1Index, mRtvDescriptorSize);

	md3dDevice->CreateRenderTargetView(mTaaHistory[0].Get(), &rtvDesc, mTaaHistoryRtv[0]);
	md3dDevice->CreateRenderTargetView(mTaaHistory[1].Get(), &rtvDesc, mTaaHistoryRtv[1]);
}

void TexColumnsApp::BuildTaaRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvRange;
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0); // t0..t4 (добавили velocity)

	CD3DX12_ROOT_PARAMETER p[3];
	p[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL); // SRVs t0..t4
	p[1].InitAsConstantBufferView(0); // b0
	p[2].InitAsConstantBufferView(1); // b1

	CD3DX12_STATIC_SAMPLER_DESC samp(
		0,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	);

	CD3DX12_ROOT_SIGNATURE_DESC desc(
		_countof(p), p,
		1, &samp,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> blob, err;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
		blob.GetAddressOf(), err.GetAddressOf());
	if (err) OutputDebugStringA((char*)err->GetBufferPointer());
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0, blob->GetBufferPointer(), blob->GetBufferSize(),
		IID_PPV_ARGS(&mTaaRootSignature)));
}



void TexColumnsApp::CreateTaaDepthHistoryTextures()
{
	mTaaDepthHistory[0].Reset();
	mTaaDepthHistory[1].Reset();

	// формат должен совпадать с mDepthStencilBuffer (R24G8_TYPELESS)
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Alignment = 0;
	d.Width = mClientWidth;
	d.Height = mClientHeight;
	d.DepthOrArraySize = 1;
	d.MipLevels = 1;
	d.Format = DXGI_FORMAT_R24G8_TYPELESS;
	d.SampleDesc.Count = 1;
	d.SampleDesc.Quality = 0;
	d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	d.Flags = D3D12_RESOURCE_FLAG_NONE; // это НЕ depth-stencil, просто копия для чтения

	for (int i = 0; i < 2; ++i)
	{
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&d,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // будем читать в TAA
			nullptr,
			IID_PPV_ARGS(&mTaaDepthHistory[i])));

		mTaaDepthHistory[i]->SetName(i == 0 ? L"TAA Depth History 0" : L"TAA Depth History 1");
		mTaaDepthHistState[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	}

	// при ресайзе history надо сбросить
	mTaaHistoryIndex = 0;
	mTaaHistoryValid = false;
}

inline void TexColumnsApp::Transition(
	ID3D12Resource* res,
	D3D12_RESOURCE_STATES& cur,
	D3D12_RESOURCE_STATES next)
{
	if (cur == next) return;

	auto b = CD3DX12_RESOURCE_BARRIER::Transition(
		res, cur, next,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	mCommandList->ResourceBarrier(1, &b);
	cur = next;
}


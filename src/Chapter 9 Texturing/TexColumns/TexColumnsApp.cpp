//***************************************************************************************
// TexColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************
#include "../../Common/Camera.h"
#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include <filesystem>
#include "FrameResource.h"
#include "RenderItem.h"
#include "Scene.h"
#include <iostream>
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui.h"
#include <commdlg.h> 
#include <filesystem> 

Scene scene;
Camera cam;
static int imguiID = 0;
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.

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
	void LoadAllTextures();
	void LoadTexture(const std::string& name);
    void BuildRootSignature();
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
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<std::unique_ptr<RenderItem>>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
	void CreateSpotLight(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 color, float faloff_start, float faloff_end, float strength, float spotpower);
	void CreatePointLight(XMFLOAT3 pos, XMFLOAT3 color, float faloff_start, float faloff_end,float strength);

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

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    PassConstants mMainPassCB;
	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = 0.2f*XM_PI;
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
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
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
    if(md3dDevice != nullptr)
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
    if(!D3DApp::Initialize())
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
	BuildLights();
	BuildShadowMapViews();
	BuildDescriptorHeaps();
    BuildShapeGeometry();
	SetLightShapes();
    BuildShadersAndInputLayout();
	BuildMaterials();
	BuildPSOs();
    BuildRenderItems();
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
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // Or your render target format.
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	init_info.SrvDescriptorHeap = m_ImGuiSrvDescriptorHeap.Get();
	init_info.LegacySingleSrvCpuDescriptor = m_ImGuiSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	init_info.LegacySingleSrvGpuDescriptor = m_ImGuiSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	ImGui_ImplWin32_Init(mhMainWnd);
	ImGui_ImplDX12_Init(&init_info);
    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();
    return true;
}
 
void TexColumnsApp::OnResize()
{
    D3DApp::OnResize();
	CreateGBuffer();
	BuildDescriptorHeaps();
    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.4*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);


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
	// === ImGui Setup ===
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGui::Begin("Control"); // wide-строка

	std::wstring initialDir = std::filesystem::current_path().parent_path().parent_path().parent_path().wstring();
	wchar_t filename[MAX_PATH] = L"";
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrInitialDir = initialDir.c_str();
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (ImGui::Button("Add Model")) {
		ofn.lpstrFilter = L"Model Files (*.obj;*.fbx)\0*.obj;*.fbx\0All Files\0*.*\0";
		
		if (GetOpenFileNameW(&ofn)) {
			// Загрузить модель по пути filename
		}
	}

	if (ImGui::Button("Save")) {
		ofn.lpstrFilter = L"Scene Files (*.json)\0*.json\0All Files\0*.*\0";

		if (GetSaveFileNameW(&ofn)) {
			// Сохранить сцену по пути filename
		}
	}

	if (ImGui::Button("Load")) {
		ofn.lpstrFilter = L"Scene Files (*.json)\0*.json\0All Files\0*.*\0";

		if (GetOpenFileNameW(&ofn)) {
			// Загрузить сцену по пути filename
		}
	}


	ImGui::End();

	ImGui::Begin("Settings");
	std::unordered_map<std::string, std::vector<RenderItem*>> groupedItems;
	ImGui::Text("Objects\n\n");
	for (auto& rItem : scene.GetRenderItems())
	{
		groupedItems[rItem->Name].push_back(rItem.get());
	}

	static std::string objectToDelete;

	for (auto& [name, items] : groupedItems)
	{
		ImGui::Text(name.c_str());

		ImGui::PushID(++imguiID);

		auto* firstItem = items[0];
		XMFLOAT3 position = firstItem->Position;
		XMFLOAT3 rotation = firstItem->RotationAngle;
		XMFLOAT3 scale = firstItem->Scale;

		static std::unordered_map<RenderItem*, bool> uniformScaleFlags;
		bool& uniformScale = uniformScaleFlags[firstItem];

		ImGui::SameLine();
		if (ImGui::Button("Delete Object")) {
			objectToDelete = name;
			scene.RemoveRenderItemsByName(name);
		}

		if (ImGui::DragFloat3("Position", (float*)&position, 0.1f))
		{
			for (auto* item : items)
				item->Position = position;
		}

		if (ImGui::DragFloat3("Rotation", (float*)&rotation, 0.05f))
		{
			for (auto* item : items)
				item->RotationAngle = rotation;
		}

		ImGui::Checkbox("Uniform Scale", &uniformScale);
		float prevScaleX = scale.x;
		float prevScaleY = scale.y;
		float prevScaleZ = scale.z;

		if (prevScaleX == 0.f) { prevScaleX = 0.001f; }
		if (prevScaleY == 0.f) { prevScaleY = 0.001f; }
		if (prevScaleZ == 0.f) { prevScaleZ = 0.001f; }
		
		
		if (ImGui::DragFloat3("Scale", (float*)&scale, 0.05f))
		{
			if (uniformScale)
			{
				float ratioX = scale.x / prevScaleX;
				float ratioY = scale.y / prevScaleY;
				float ratioZ = scale.z / prevScaleZ;
				float avgRatio = (ratioX + ratioY + ratioZ) / 3.0f;

				scale.x = prevScaleX * avgRatio;
				scale.y = prevScaleY * avgRatio;
				scale.z = prevScaleZ * avgRatio;
			}

			for (auto* item : items)
				item->Scale = scale;
		}

		ImGui::PopID();

		for (auto* item : items)
		{
			item->TranslationM = XMMatrixTranslation(item->Position.x, item->Position.y, item->Position.z);
			item->RotationM = XMMatrixRotationRollPitchYaw(item->RotationAngle.x, item->RotationAngle.y, item->RotationAngle.z);
			item->ScaleM = XMMatrixScaling(item->Scale.x, item->Scale.y, item->Scale.z);
			XMStoreFloat4x4(&item->World, item->ScaleM * item->RotationM * item->TranslationM);
			item->NumFramesDirty = gNumFrameResources;
		}
	}

	ImGui::Text("\n\nLights\n\n");

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateLightCBs(gt);
	UpdateMainPassCB(gt);
	ImGui::End();
}




void TexColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
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
	for(auto& e : scene.GetRenderItems())
	{

		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.InvWorld,MathHelper::InverseTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void TexColumnsApp::UpdateLightCBs(const GameTimer& gt)
{
	
	auto currLightCB = mCurrFrameResource->LightCB.get();
	auto currShadowCB = mCurrFrameResource->PassShadowCB.get();
	int lId = 0;
	
	for (int i = 0; i < scene.LightsGetSize(); )
	{
		Light& l = *scene.GetLightByIndex(i);
		LightConstants lConst;
		PassShadowConstants shConst;
		if (l.type == 0)
		{
			//l.Color = mLights[0].Color; // ambient light equals directional;
			std::string s = "\Ambient Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			ImGui::DragFloat("Strength", (float*)&l.Strength,0.02f);
			ImGui::PopID();
			
			lId++;
			i++;

		}
		else if (l.type == 1)
		{
			std::string s = "\nPoint Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			if (ImGui::Button("Delete Point Light")) {
				scene.RemoveLightsByIndex(i);
				ImGui::PopID();
				continue;
			}
			float* a[] = { &l.Position.x,&l.Position.y,&l.Position.z };
			XMStoreFloat4x4(&l.gWorld, XMMatrixTranspose(XMMatrixScaling(l.FalloffEnd * 2, l.FalloffEnd * 2, l.FalloffEnd * 2) * XMMatrixTranslation(l.Position.x, l.Position.y, l.Position.z)));
			
			ImGui::DragFloat3("Position", *a, 0.1f, -100,100);
			
			ImGui::ColorEdit3("Color", (float*)&l.Color);
			
			ImGui::DragFloat("Strength", &l.Strength,0.1f,0,100);
			
			ImGui::DragFloat("FaloffStart", &l.FalloffStart,0.1f, 1, l.FalloffEnd);
			
			ImGui::DragFloat("FaloffEnd", &l.FalloffEnd, 0.1f, l.FalloffStart, 100);
			
			bool b = l.isDebugOn;
			ImGui::Checkbox("is Debug On", &b);
			l.isDebugOn = b;
			
			ImGui::PopID();

			lId++;
			i++;
		
			l.Position.z = sin(gt.TotalTime()*3)*6;
		}
		else if (l.type == 2)
		{
			std::string s = "\nDirectional Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			if(ImGui::Button("Delete Directional Light")) {
				scene.RemoveLightsByIndex(i);
				ImGui::PopID();
				continue;
			}
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

			ImGui::PopID();

			lId++;
			i++;
			
		}
		else if (l.type == 3)
		{
			std::string s = "\nSpot Light " + std::to_string(lId);
			ImGui::PushID(++imguiID);
			ImGui::Text(s.c_str());
			if (ImGui::Button("Delete Spot Light")) {
				scene.RemoveLightsByIndex(i);
				ImGui::PopID();
				continue;
			}
			float* a[] = { &l.Position.x,&l.Position.y,&l.Position.z };
			ImGui::DragFloat3("Position", (float*)&l.Position, 0.1f, -100, 100);

			ImGui::DragFloat3("Rotation", (float*)&l.Rotation, 0.1f, -180, 180);
			XMStoreFloat4x4(&l.gWorld, XMMatrixTranspose(XMMatrixScaling(l.FalloffEnd*4/3, l.FalloffEnd,l.FalloffEnd*4/3) * XMMatrixTranslation(0, -l.FalloffEnd/2, 0) *
				XMMatrixRotationRollPitchYaw(XMConvertToRadians(l.Rotation.x), XMConvertToRadians(l.Rotation.y), XMConvertToRadians(l.Rotation.z)) *
				XMMatrixTranslation(l.Position.x, l.Position.y, l.Position.z)));
			XMFLOAT3 d(0, -1, 0);
			XMVECTOR v = XMLoadFloat3(&d);
			
			v = XMVector3TransformNormal(v, XMMatrixRotationRollPitchYaw(XMConvertToRadians(l.Rotation.x),XMConvertToRadians(l.Rotation.y),XMConvertToRadians(l.Rotation.z)));
			
			XMStoreFloat3(&l.Direction, v);
			d = XMFLOAT3(-1, 0, 0);
			v = XMLoadFloat3(&d);
			v = XMVector3TransformNormal(v, XMMatrixRotationRollPitchYaw(XMConvertToRadians(l.Rotation.x), XMConvertToRadians(l.Rotation.y), XMConvertToRadians(l.Rotation.z)));
			l.LightUp = v;

			ImGui::ColorEdit3("Color", (float*)&l.Color);

			ImGui::DragFloat("Strength", &l.Strength, 0.1f, 0, 100);

			ImGui::DragFloat("Faloff Start", &l.FalloffStart, 0.1f, 0,100);
	
			ImGui::DragFloat("Faloff End", &l.FalloffEnd,0.1f, 0, 100);
		
			ImGui::SliderFloat("Spot Power", &l.SpotPower, 0, 10);
			
			ImGui::DragInt("PCF level", &l.pcf_level, 1, 0, 100);

			bool c = l.enablePCF;
			ImGui::Checkbox("Enable PCF", &c);
			l.enablePCF = c;

			bool b = l.CastsShadows;
			ImGui::Checkbox("Cast Shadows", &b);
			l.CastsShadows = b;
		
			b = l.isDebugOn;
			ImGui::Checkbox("is Debug On", &b);
			l.isDebugOn = b;
			ImGui::PopID();
			
			lId++;
			i++;

		}

		
		if (l.type == 2 && l.CastsShadows || l.type == 3 && l.CastsShadows) // Directional Light
		{
			// Create an orthographic projection for the directional light->
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
			float viewWidth = 600.0f; // Adjust to fit your scene
			float viewHeight = 600.0f;
			float nearZ = 1.0f;
			float farZ = 1000.0f; // Adjust
			XMMATRIX lightProj = XMMatrixIdentity();
			if (l.type == 2)
				lightProj = XMMatrixOrthographicLH(viewWidth, viewHeight, nearZ, farZ);
			else 
				lightProj = XMMatrixPerspectiveFovLH(0.5f * MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
			XMStoreFloat4x4(&l.LightProj, lightProj);
			XMStoreFloat4x4(&l.LightViewProj, XMMatrixTranspose(XMMatrixMultiply(lightView,lightProj)));
		}
		lConst.light = l;
		shConst.LightViewProj = l.LightViewProj;
		currShadowCB->CopyData(l.LightCBIndex,shConst);
		currLightCB->CopyData(l.LightCBIndex, lConst);
		
	}
}

void TexColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TexColumnsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

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
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TexColumnsApp::CreateGBuffer()
{
	// Форматы
	const DXGI_FORMAT positionFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	const DXGI_FORMAT normalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	const DXGI_FORMAT albedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	FlushCommandQueue();

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	// Описание ресурса
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mClientWidth;
	texDesc.Height = mClientHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ;

	mGBufferPosition.Reset();
	mGBufferNormal.Reset();
	mGBufferAlbedo.Reset();
	// Создание ресурсов --------------------------------------------------------
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

	// Создание RTV -------------------------------------------------------------
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		SwapChainBufferCount, // Начинаем после SwapChain
		mRtvDescriptorSize
	);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	// Albedo
	rtvDesc.Format = albedoFormat;
	md3dDevice->CreateRenderTargetView(mGBufferAlbedo.Get(), &rtvDesc, rtvHandle);
	mGBufferRTVs[0] = rtvHandle;
	rtvHandle.Offset(1, mRtvDescriptorSize);

	// Normal
	rtvDesc.Format = normalFormat;
	md3dDevice->CreateRenderTargetView(mGBufferNormal.Get(), &rtvDesc, rtvHandle);
	mGBufferRTVs[1] = rtvHandle;
	rtvHandle.Offset(1, mRtvDescriptorSize);
	// Position
	rtvDesc.Format = positionFormat;
	md3dDevice->CreateRenderTargetView(mGBufferPosition.Get(), &rtvDesc, rtvHandle);
	mGBufferRTVs[2] = rtvHandle;


	// Execute the resize commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until resize is complete.
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
			filepath = filepath.substr(0, filepath.size()-4);
			filepath = "textures/" + filepath;
			LoadTexture(filepath);
		}
	}
}

void TexColumnsApp::LoadTexture(const std::string& name)
{
	auto tex = std::make_unique<Texture>();
	tex->Name = name;
	tex->Filename = L"../../Textures/" + std::wstring(name.begin(), name.end()) + L".dds";
	
	if (FAILED(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tex->Filename.c_str(),
		tex->Resource, tex->UploadHeap))) std::cout << name << "\n";
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

    if(errorBlob != nullptr)
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

// build lighting root signature 
void TexColumnsApp::BuildLightingRootSignature()
{


	CD3DX12_DESCRIPTOR_RANGE gPosition;
	gPosition.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
	CD3DX12_DESCRIPTOR_RANGE gNormal;
	gNormal.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1
	CD3DX12_DESCRIPTOR_RANGE gAlbedo;
	gAlbedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2
	CD3DX12_DESCRIPTOR_RANGE shadowMapRange;
	shadowMapRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // Shadow map at register t3

	CD3DX12_ROOT_PARAMETER rootParams[7];
	rootParams[0].InitAsDescriptorTable(1, &gPosition, D3D12_SHADER_VISIBILITY_ALL);
	rootParams[1].InitAsDescriptorTable(1, &gNormal, D3D12_SHADER_VISIBILITY_ALL);
	rootParams[2].InitAsDescriptorTable(1, &gAlbedo, D3D12_SHADER_VISIBILITY_ALL);
	rootParams[3].InitAsConstantBufferView(0); // b0 
	rootParams[4].InitAsConstantBufferView(1); // b1
	rootParams[5].InitAsConstantBufferView(2); // b2
	rootParams[6].InitAsDescriptorTable(1, &shadowMapRange, D3D12_SHADER_VISIBILITY_PIXEL); // PIXEL visibility

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(_countof(rootParams), rootParams,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);


	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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


void TexColumnsApp::CreatePointLight(XMFLOAT3 pos, XMFLOAT3 color, float faloff_start, float faloff_end, float strength)
{
	auto light = std::make_unique <Light>();
	light->LightCBIndex = scene.LightsGetSize();

	light->Position = pos;
	light->Color = color;
	light->FalloffStart = faloff_start;
	light->FalloffEnd = faloff_end;
	light->type = 1;
	auto& world = XMMatrixScaling(faloff_end * 2, faloff_end * 2, faloff_end * 2) * XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMStoreFloat4x4(&light->gWorld, XMMatrixTranspose(world));
	scene.AddLight(std::move(light));
}

void TexColumnsApp::CreateSpotLight(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 color, float faloff_start, float faloff_end, float strength, float spotpower)
{
	auto light = std::make_unique <Light>();
	light->LightCBIndex = scene.LightsGetSize();

	light->Position = pos;
	light->Color = color;
	light->FalloffStart = faloff_start;
	light->FalloffEnd = faloff_end;
	light->Rotation = rot;
	light->LightUp = XMVectorSet(0, 1, 0, 0);
	light->type = 3;
	light->Strength = strength;
	light->SpotPower = spotpower;
	scene.AddLight(std::move(light));
}

void TexColumnsApp::BuildLights()
{
	// directional
	auto dir = std::make_unique <Light>();
	dir->LightCBIndex = scene.GetLights().size();
	dir->Position = { 0,300,0 };
	dir->Direction = { 0, -1, 0 };
	dir->Color = { 1,1,1 };
	dir->Strength = 1.2;
	dir->type = 2;
	dir->LightUp = XMVectorSet(0, 0, -1, 0);
	auto& world = XMMatrixScaling(1000,1000,1000);
	XMStoreFloat4x4(&dir->gWorld, XMMatrixTranspose(world));
	scene.AddLight(std::move(dir));

	auto ambient = std::make_unique <Light>();
	ambient->LightCBIndex = scene.GetLights().size();
	ambient->Position = { 3.0f, 0.0f, 3.0f };
	ambient->Color = { 0,0,0 }; // need only x
	ambient->Strength = 0.2; // need only x
	ambient->type = 0;
	XMStoreFloat4x4(&ambient->gWorld, XMMatrixTranspose(XMMatrixTranslation(0, 0, 0) * XMMatrixScaling(1000, 1000, 1000)));
	scene.AddLight(std::move(ambient));

	CreatePointLight({ -3,3,0 }, { 4,0,0 }, 1, 5,1);
	CreatePointLight({ 3,3,0 }, { 0,0,4 }, 1, 5,1);

	CreateSpotLight({ -5,3,30 }, { 0,0,-90 }, { 1,1,1 }, 1, 30, 6, 1);
}

void TexColumnsApp::SetLightShapes()
{
	for (auto& light : scene.GetLights())
	{

		switch (light->type)
		{
		case 1:
			light->ShapeGeo = mGeometries["shapeGeo"]->DrawArgs["sphere"];
			break;
		case 3:
			light->ShapeGeo = mGeometries["shapeGeo"]->DrawArgs["box"];
			break;
		}
	}
	scene.GetLights();
}

void TexColumnsApp::CreateMaterial(std::string _name, int _CBIndex, int _SRVDiffIndex, int _SRVNMapIndex, XMFLOAT4 _DiffuseAlbedo, XMFLOAT3 _FresnelR0, float _Roughness)
{
	
	auto material = std::make_unique<Material>();
	material->Name = _name;
	material->MatCBIndex = static_cast<int>(mMaterials.size());
	material->DiffuseSrvHeapIndex = _SRVDiffIndex;
	material->NormalSrvHeapIndex = _SRVNMapIndex;
	material->DiffuseAlbedo = _DiffuseAlbedo;
	material->FresnelR0 = _FresnelR0;
	material->Roughness = _Roughness;
	mMaterials[_name] = std::move(material);
}

void TexColumnsApp::BuildShadowMapViews()
{
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 2; // For one shadow map
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));
	int i = 0;
	for (auto& light : scene.GetLights())
	{

		if (light->type == 2 || light->type == 3)
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
				D3D12_RESOURCE_STATE_GENERIC_READ, // Start in generic read, will transition to DEPTH_WRITE
				&clearValue,
				IID_PPV_ARGS(&light->ShadowMap)));
			// Create DSV for the shadow map.
			// We need a DSV heap. Let's create one specifically for shadow maps for clarity,
			// or you can extend your existing mDsvHeap if it's not solely for the main depth buffer.
			
			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
			dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Format = SHADOW_MAP_DSV_FORMAT;
			dsvDesc.Texture2D.MipSlice = 0;
			light->ShadowMapDsvHandle = mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
			light->ShadowMapDsvHandle.Offset(i, mDsvDescriptorSize); // Use the stored index
			md3dDevice->CreateDepthStencilView(light->ShadowMap.Get(), &dsvDesc, light->ShadowMapDsvHandle);

			light->ShadowMapSrvHeapIndex = mTextures.size() + 3 + i;
			i++;
		}
	}
	scene.GetLights();

	


	
}

void TexColumnsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = mTextures.size() + 3 + scene.GetLights().size();
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));


	// Создание SRV -------------------------------------------------------------

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	int offset = 0;
	for (const auto& tex : mTextures) {
		auto text = tex.second->Resource;
		DXGI_FORMAT format = text->GetDesc().Format;
		if (format == DXGI_FORMAT_UNKNOWN) {
			abort();
		}
		srvDesc.Format = text->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = text->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(text.Get(), &srvDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvDescriptorSize);
		TexOffsets[tex.first] = offset;
		offset++;
	}
	srvDesc.Texture2D.MipLevels = 1;
	// Albedo SRV
	srvDesc.Format = albedoFormat;
	md3dDevice->CreateShaderResourceView(
		mGBufferAlbedo.Get(), &srvDesc, hDescriptor);
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	// Normal SRV
	srvDesc.Format = normalFormat;
	md3dDevice->CreateShaderResourceView(
		mGBufferNormal.Get(), &srvDesc, hDescriptor);
	//mGBufferSRVs[1] = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvHandle);
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	// Position SRV
	srvDesc.Format = positionFormat;
	md3dDevice->CreateShaderResourceView(
		mGBufferPosition.Get(), &srvDesc, hDescriptor);
	for (auto& light : scene.GetLights())
	{
		if (light->type == 2 || light->type == 3)
		{
			srvDesc.Format = SHADOW_MAP_SRV_FORMAT; // Use the SRV-compatible format
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			CD3DX12_CPU_DESCRIPTOR_HANDLE shadowMapSrvHandle(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
			shadowMapSrvHandle.Offset(light->ShadowMapSrvHeapIndex, mCbvSrvDescriptorSize); // Use the stored index

			md3dDevice->CreateShaderResourceView(light->ShadowMap.Get(), &srvDesc, shadowMapSrvHandle);
		}
	}
	


	HRESULT hr = md3dDevice->GetDeviceRemovedReason();
	if (FAILED(hr))
	{
		std::cout << "Error creating SRV: " << std::hex << hr << std::endl;
	}
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
	mShaders["lightingVS"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["lightingQUADVS"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "VS_QUAD", "vs_5_0");
	mShaders["lightingPS"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "PS", "ps_5_0");
	mShaders["lightingPSDebug"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "PS_debug", "ps_5_0");
	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\ShadowMap.hlsl", nullptr, "VS", "vs_5_1");

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
	
	for (int i = 0;i < scene->mNumMeshes;i++)
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

		// Заполняем meshData. Здесь тебе нужно адаптировать под свою структуру:
		meshData.Vertices = vertices;
		meshData.Indices32.resize(indices.size());
		for (size_t j = 0; j < indices.size(); ++j)
			meshData.Indices32[j] = indices[j];

		aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
		aiString texturePath;

		aiString texPath;

		meshData.matName = scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str();
		// Если требуется, можно выполнить дополнительные операции, например, нормализацию, вычисление тангенсов и т.д.
		meshDatas.push_back(meshData);
	}
	for (int k = 0;k < scene->mNumMaterials;k++)
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

		CreateMaterial(scene->mMaterials[k]->GetName().C_Str(), k, TexOffsets[a], TexOffsets[b], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
	}

	UINT totalMeshSize = 0;
	UINT k = vertices.size();
	std::vector<std::pair<GeometryGenerator::MeshData,SubmeshGeometry>>meshSubmeshes;
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
		meshSubmeshes.push_back(std::make_pair(m,meshSubmesh));
	}
	/////////
	/////
	for (auto mesh : meshDatas)
	{
		for (size_t i = 0; i < mesh.Vertices.size(); ++i, ++k)
		{
			vertices.push_back(Vertex(mesh.Vertices[i].Position, mesh.Vertices[i].Normal, mesh.Vertices[i].TexC,mesh.Vertices[i].TangentU));
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

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//
	
	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	
	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
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

void TexColumnsApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; // Изменяем Solid на Wireframe

	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// Geometry pass PSO

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gbPsoDesc = {};
	gbPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	gbPsoDesc.pRootSignature = mRootSignature.Get(); // используем модифицированную корневую сигнатуру
	gbPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["gbufferVS"]->GetBufferPointer()),
					 mShaders["gbufferVS"]->GetBufferSize() };
	gbPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["gbufferPS"]->GetBufferPointer()),
					 mShaders["gbufferPS"]->GetBufferSize() };
	gbPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gbPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	// Отключаем прозрачность (или настраиваем, если нужны)
	gbPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	gbPsoDesc.SampleMask = UINT_MAX;
	gbPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// Теперь указываем несколько рендер-таргетов (G-Buffer)
	gbPsoDesc.NumRenderTargets = 3;
	gbPsoDesc.RTVFormats[0] = albedoFormat;     // альбедо
	gbPsoDesc.RTVFormats[1] = normalFormat; // нормали
	gbPsoDesc.RTVFormats[2] = positionFormat; // позиция
	gbPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	gbPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	gbPsoDesc.DSVFormat = mDepthStencilFormat; // з-дефолтовый формат глубины (может быть D32_FLOAT)

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&gbPsoDesc, IID_PPV_ARGS(&mPSOs["gbuffer"])));

	// Lighting pass PSO


	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightPsoDesc = {};
	lightPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() }; // если используем SV_VertexID в шейдере, входного layout не нужно
	lightPsoDesc.pRootSignature = mLightingRootSignature.Get(); // наша новая корнев. сигнатура для освещения
	lightPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["lightingVS"]->GetBufferPointer()),
						mShaders["lightingVS"]->GetBufferSize() };
	lightPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["lightingPS"]->GetBufferPointer()),
						mShaders["lightingPS"]->GetBufferSize() };
	lightPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	lightPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;

	D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc = {};
	rtBlendDesc.BlendEnable = TRUE;                         // включаем смешивание
	rtBlendDesc.LogicOpEnable = FALSE;
	rtBlendDesc.SrcBlend = D3D12_BLEND_ONE;              // src * 1
	rtBlendDesc.DestBlend = D3D12_BLEND_ONE;              // dest * 1
	rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;           // сложение
	rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;             // альфа сохраняем из src
	rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rtBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // RGB + A

	D3D12_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0] = rtBlendDesc;
	lightPsoDesc.BlendState = blendDesc;




	lightPsoDesc.SampleMask = UINT_MAX;
	lightPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	
	lightPsoDesc.NumRenderTargets = 1;                   // выводим один финальный цвет
	lightPsoDesc.RTVFormats[0] = mBackBufferFormat;      // формат экрана (обычно DXGI_FORMAT_R8G8B8A8_UNORM)
	lightPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	lightPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	lightPsoDesc.DSVFormat = mDepthStencilFormat; // не используем буфер глубины

	//D3D12_DEPTH_STENCIL_DESC dsDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	//dsDesc.DepthEnable = TRUE;
	//dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // можно отключить запись, но оставить тест
	//dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	//lightPsoDesc.DepthStencilState = dsDesc;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&lightPsoDesc, IID_PPV_ARGS(&mPSOs["lighting"])));

	// Lighting(QUAD) pass PSO


	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightQUADPsoDesc = lightPsoDesc;
	lightQUADPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	lightQUADPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["lightingQUADVS"]->GetBufferPointer()),
						mShaders["lightingQUADVS"]->GetBufferSize() };
	
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&lightQUADPsoDesc, IID_PPV_ARGS(&mPSOs["lightingQUAD"])));
	// Debug lighting shapes PSO

	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightShapesPsoDesc = lightPsoDesc;
	lightShapesPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	lightShapesPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	D3D12_DEPTH_STENCIL_DESC dsDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	dsDesc.DepthEnable = TRUE;
	dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // можно отключить запись, но оставить тест
	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	lightShapesPsoDesc.DepthStencilState = dsDesc;
	lightShapesPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["lightingPSDebug"]->GetBufferPointer()),
						mShaders["lightingPSDebug"]->GetBufferSize() };

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&lightShapesPsoDesc, IID_PPV_ARGS(&mPSOs["lightingShapes"])));

	// PSO for shadow map pass
	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = {};
	shadowPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() }; // Same input layout
	shadowPsoDesc.pRootSignature = mShadowPassRootSignature.Get();
	shadowPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
		mShaders["shadowVS"]->GetBufferSize()
	};
	// shadowPsoDesc.PS can be omitted if no pixel shader (Null PS)
	// If you have an alpha testing PS:
	// shadowPsoDesc.PS =
	// {
	//    reinterpret_cast<BYTE*>(mShaders["shadowPS"]->GetBufferPointer()),
	//    mShaders["shadowPS"]->GetBufferSize()
	// };
	shadowPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	// You might need to tweak RasterizerState for shadow acne (DepthBias, SlopeScaledDepthBias)
	// e.g., shadowPsoDesc.RasterizerState.DepthBias = 100000; // Experiment with values
	// shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	// shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f; // Experiment

	shadowPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // No color writing
	shadowPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	shadowPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	shadowPsoDesc.SampleMask = UINT_MAX;
	shadowPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	shadowPsoDesc.NumRenderTargets = 0; // Not writing to any color targets
	shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	shadowPsoDesc.SampleDesc.Count = 1; // No MSAA for shadow map
	shadowPsoDesc.SampleDesc.Quality = 0;
	shadowPsoDesc.DSVFormat = SHADOW_MAP_DSV_FORMAT; // Format of our shadow map DSV

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_map"])));


}

void TexColumnsApp::BuildFrameResources()
{
	FlushCommandQueue();
	mFrameResources.clear();
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)scene.GetRenderItems().size(), (UINT)mMaterials.size(),(UINT)scene.GetLights().size()));
    }
	mCurrFrameResourceIndex = 0;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
	for (auto& ri : scene.GetRenderItems())
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
	CreateMaterial("NiggaMat",0, TexOffsets["textures/texture"], TexOffsets["textures/texture_nm"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
	CreateMaterial("eye",0, TexOffsets["textures/eye"], TexOffsets["textures/eye_nm"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
	CreateMaterial("map",0, TexOffsets["textures/HeightMap2"], TexOffsets["textures/HeightMap2"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
	CreateMaterial("map2",0, TexOffsets["textures/HeightMap"], TexOffsets["textures/HeightMap"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
	//CreateMaterial("bricks",0, TexOffsets["textures/bricks"], TexOffsets["textures/bricks"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
	CreateMaterial("prikol1",0, TexOffsets["textures/prikol2"], TexOffsets["textures/prikol2"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
}
void TexColumnsApp::RenderCustomMesh(std::string unique_name, std::string meshname, std::string materialName, XMFLOAT3 Scale, XMFLOAT3 Rotation, XMFLOAT3 Position)
{
	for (int i = 0;i < ObjectsMeshCount[meshname];i++)
	{
		auto rItem = std::make_unique<RenderItem>();
		std::string textureFile;
		rItem->Name = unique_name;
		auto trans = XMMatrixTranslation(Position.x, Position.y, Position.z);
		auto rot = XMMatrixRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z);
		auto scl = XMMatrixScaling(Scale.x, Scale.y, Scale.z);
		XMStoreFloat4x4(&rItem->TexTransform, XMMatrixScaling(1, 1., 1.));
		XMStoreFloat4x4(&rItem->World, scl * rot * trans);
		rItem->TranslationM =  trans;
		rItem->RotationM = rot;
		rItem->ScaleM = scl;

		rItem->Position = Position;
		rItem->RotationAngle = Rotation;
		rItem->Scale = Scale;
		rItem->ObjCBIndex = scene.GetRenderItems().size();
		rItem->Geo = mGeometries["shapeGeo"].get();
		rItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		std::string matname = rItem->Geo->MultiDrawArgs[meshname][i].first.matName;
		std::cout << " mat : " << matname << "\n";
		std::cout << unique_name << " " << matname << "\n";
		if (materialName != "") matname = materialName;
		rItem->Mat = mMaterials[matname].get();
		rItem->IndexCount = rItem->Geo->MultiDrawArgs[meshname][i].second.IndexCount;
		rItem->StartIndexLocation = rItem->Geo->MultiDrawArgs[meshname][i].second.StartIndexLocation;
		rItem->BaseVertexLocation = rItem->Geo->MultiDrawArgs[meshname][i].second.BaseVertexLocation;
		scene.GetRenderItems().push_back(std::move(rItem));
	}
	
}



void TexColumnsApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();
	boxRitem->Name = "box";
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 5.0f, -10.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1,1,1));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Mat = mMaterials["NiggaMat"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	scene.AddRenderItem(std::move(boxRitem));

	RenderCustomMesh("sponza", "sponza", "", XMFLOAT3(0.07, 0.07, 0.07), XMFLOAT3(0, 3.14 / 2, 0), XMFLOAT3(0, 0, 0));
	RenderCustomMesh("nigga", "negr", "NiggaMat", XMFLOAT3(3, 3, 3), XMFLOAT3(0, 3.14, 0), XMFLOAT3(0, 3, 0));
	RenderCustomMesh("nigga2", "negr", "NiggaMat", XMFLOAT3(3, 3, 3), XMFLOAT3(0, -3.14 / 2, 0), XMFLOAT3(-10, 3, 30));
	RenderCustomMesh("eyeL", "left", "eye", XMFLOAT3(3, 3, 3), XMFLOAT3(0, 3.14, 0), XMFLOAT3());
	RenderCustomMesh("eyeR", "right", "eye", XMFLOAT3(3, 3, 3), XMFLOAT3(0, 3.14, 0), XMFLOAT3());
	BuildFrameResources();
	// All the render items are opaque.
	for (auto& e : scene.GetRenderItems())
	{
		if (e->Name == "plan")
		{
			XMStoreFloat4x4(&e->TexTransform, XMMatrixScaling(1, 1, 1));
		}
		//scene.AddRenderItem(e.get());
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


	DrawRenderItems(mCommandList.Get(), scene.GetRenderItems());


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
	for (auto& lightPtr : scene.GetLights())
	{
		auto* light = lightPtr.get();
		if (light->type == 2 || light->type == 3)
		{
			if (light->CastsShadows)
			{
				mCommandList->SetPipelineState(mPSOs["shadow_map"].Get());
				mCommandList->SetGraphicsRootSignature(mShadowPassRootSignature.Get());
				// Set the viewport and scissor rect for the shadow map.
				mCommandList->RSSetViewports(1, &mShadowViewport);
				mCommandList->RSSetScissorRects(1, &mShadowScissorRect);
				// Transition the shadow map from generic read to depth-write.
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(light->ShadowMap.Get(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

				// Clear the shadow map.
				mCommandList->ClearDepthStencilView(light->ShadowMapDsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

				// Set the shadow map as the depth-stencil buffer. No render targets.
				mCommandList->OMSetRenderTargets(0, nullptr, FALSE, &light->ShadowMapDsvHandle);

				D3D12_GPU_VIRTUAL_ADDRESS shadowCBAddress = mCurrFrameResource->PassShadowCB->Resource()->GetGPUVirtualAddress() + light->LightCBIndex * shadowCBByteSize;
				mCommandList->SetGraphicsRootConstantBufferView(1, shadowCBAddress);
				// Draw all opaque items.
				UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

				auto objectCB = mCurrFrameResource->ObjectCB->Resource();

				// For each render item...
				for (size_t i = 0; i < scene.GetRenderItems().size(); ++i)
				{
					auto ri = scene.GetRenderItems()[i].get();
					mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
					mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
					mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

					D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
					mCommandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

					mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
				}
				// Transition the shadow map from depth-write to pixel shader resource for the lighting pass.
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(light->ShadowMap.Get(),
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


	// draw shadow maps 
	DrawSceneToShadowMap();


	// ==GEOMETRY PASS==
	mCommandList->SetPipelineState(mPSOs["gbuffer"].Get());


	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Обнуляем буферы G-Buffer
	// Очищаем каждый G-Buffer и глубину
	// Стало:
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHs[] = {
	CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		SwapChainBufferCount, // Начинаем после SwapChain
		mRtvDescriptorSize
	),
	CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		SwapChainBufferCount + 1, // Начинаем после SwapChain
		mRtvDescriptorSize
	),
	CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		SwapChainBufferCount + 2, // Начинаем после SwapChain
		mRtvDescriptorSize
	) };
	XMFLOAT4 c(scene.GetLights()[0]->Color.x, scene.GetLights()[0]->Color.y, scene.GetLights()[0]->Color.z,1);
	XMVECTORF32 a;
	a.v = XMLoadFloat4(&c);
	for (int i = 0; i < 3; ++i)
		mCommandList->ClearRenderTargetView(rtvHs[i], a, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->OMSetRenderTargets(3, rtvHs, true, &DepthStencilView());
	
	ID3D12DescriptorHeap* heaps[] = { mSrvDescriptorHeap.Get() /*для текстур*/ };
	mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), scene.GetRenderItems());


	D3D12_RESOURCE_BARRIER barrier[3] = {
	CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
	CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
	CD3DX12_RESOURCE_BARRIER::Transition(mGBufferPosition.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};
	mCommandList->ResourceBarrier(3, barrier);
	// ================================================
	
	// ===============LIGHTING PASS=====================

	mCommandList->SetPipelineState(mPSOs["lighting"].Get());
	
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);
	



	mCommandList->SetGraphicsRootSignature(mLightingRootSignature.Get());
	
	mCommandList->SetDescriptorHeaps(1, mSrvDescriptorHeap.GetAddressOf());


	CD3DX12_GPU_DESCRIPTOR_HANDLE positionHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	positionHandle.Offset(mTextures.size() + 0, mCbvSrvDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE normalHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	normalHandle.Offset(mTextures.size() + 1, mCbvSrvDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE albedoHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	albedoHandle.Offset(mTextures.size() + 2, mCbvSrvDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(0, positionHandle); // t0
	mCommandList->SetGraphicsRootDescriptorTable(1, normalHandle); // t1
	mCommandList->SetGraphicsRootDescriptorTable(2, albedoHandle); // t2
	mCommandList->SetGraphicsRootConstantBufferView(3, mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress()); //b0
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	
	
	UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(LightConstants));
	// draw light
	for (auto& light : scene.GetLights())
	{
		auto lightCB = mCurrFrameResource->LightCB->Resource();
		mCommandList->IASetVertexBuffers(0, 1, &mGeometries["shapeGeo"]->VertexBufferView());
		mCommandList->IASetIndexBuffer(&mGeometries["shapeGeo"]->IndexBufferView());

		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + light->LightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(5, lightCBAddress); // b2

		if (light->CastsShadows) // Only bind shadow map if this light uses it
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			shadowSrvHandle.Offset(light->ShadowMapSrvHeapIndex, mCbvSrvDescriptorSize);
			mCommandList->SetGraphicsRootDescriptorTable(6, shadowSrvHandle); // t3
		}
		// if directional or ambient -> rendering full screen quad
		if (light->type == 0 || light->type == 2 )
		{
			mCommandList->SetPipelineState(mPSOs["lightingQUAD"].Get());
			mCommandList->DrawInstanced(3, 1, 0, 0);
		}
		else
		{
			mCommandList->SetPipelineState(mPSOs["lighting"].Get());
			mCommandList->DrawIndexedInstanced(light->ShapeGeo.IndexCount, 1, light->ShapeGeo.StartIndexLocation, light->ShapeGeo.BaseVertexLocation, 0);
		}
	}


	// draw light shapes
	mCommandList->SetPipelineState(mPSOs["lightingShapes"].Get());
	for (auto& light : scene.GetLights())
	{
		if (light->type != 0 && light->type != 2 && light->isDebugOn == 1)
		{
			auto lightCB = mCurrFrameResource->LightCB->Resource();
			mCommandList->IASetVertexBuffers(0, 1, &mGeometries["shapeGeo"]->VertexBufferView());
			mCommandList->IASetIndexBuffer(&mGeometries["shapeGeo"]->IndexBufferView());

			D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + light->LightCBIndex * lightCBByteSize;
			mCommandList->SetGraphicsRootConstantBufferView(5, lightCBAddress);

			mCommandList->DrawIndexedInstanced(light->ShapeGeo.IndexCount, 1, light->ShapeGeo.StartIndexLocation, light->ShapeGeo.BaseVertexLocation, 0);
		}
		
	}
	

	// После освещения:
	D3D12_RESOURCE_BARRIER revertBarrier[3] = {
		CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(mGBufferPosition.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
	};
	mCommandList->ResourceBarrier(3, revertBarrier);

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	D3D12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &presentBarrier);


	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);

}




void TexColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<std::unique_ptr<RenderItem>>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	for (const auto& riPtr : ritems)
	{
		RenderItem* ri = riPtr.get();

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


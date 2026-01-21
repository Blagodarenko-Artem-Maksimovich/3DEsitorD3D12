#include <windows.h>
#include "CGLAB.h"

#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui.h"

static int imguiID = 0;
const int gNumFrameResources = 3;
float blendfactor = 0.01f;

static int count = 0;
static float tr = 0.01;


// Main application entry point.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		CGLAB theApp(hInstance);
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


CGLAB::CGLAB(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	mSceneBounds.Center = mConfig.SceneBoundsCenter;
	mSceneBounds.Radius = mConfig.SceneBoundsRadius;
}
CGLAB::~CGLAB()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}




//==============================================================FRAMEWORK METHODS==============================================================


/*
INITIALIZATION
*/
bool CGLAB::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// reset command list
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// TODO - make full camera initialization in separate method
	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	mGeomMgr = std::make_unique<GeometryManager>();
	mGeomMgr->BuildBasicGeometry(md3dDevice.Get(), mCommandList.Get());
	mGeomMgr->BuildGeometryFromFile(md3dDevice.Get(), mCommandList.Get(), "Models/skull.obj", "skullGeo");

	mResourceMgr = std::make_unique<ResourceManager>();
	mResourceMgr->Init(md3dDevice.Get(), mCommandList.Get());
	mResourceMgr->LoadTextures();

	BuildRootSignature();
	BuildShadowsRootSignature();
	BuildGeometryRootSignature();
	BuildLightingRootSignature();
	BuildTAARootSignature();
	BuildDescriptorHeaps();
	mResourceMgr->BuildMaterials();

	BuildShadersAndInputLayout();
	BuildLights();
	SetLightShapes();
	BuildGBuffer();
	mPrevTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	mCurrentTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	mJitteredTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	mVelocityTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();
	ImguiInit();

	// execute
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	OnResize();

	// wait
	FlushCommandQueue();
	mTAAConstants.blendFactor = 0.01f;

	GenerateTransformedHaltonSequence(mClientWidth, mClientHeight, jitters);

	return true;
}

void CGLAB::ImguiInit()
{
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
}


/*
* MAIN METHODS
*/
void CGLAB::OnResize()
{
	D3DApp::OnResize();
	BuildDescriptorHeaps();
	BuildGBuffer();

	mPrevTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	mCurrentTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	mJitteredTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	mVelocityTexture = std::make_unique<ShaderTexture>(md3dDevice.Get(), mClientWidth, mClientHeight);
	BuildTAATextures();
	mCamera.SetLens(mConfig.CameraFovY, AspectRatio(), mConfig.CameraNearZ, mConfig.CameraFarZ);
}

void CGLAB::Update(const GameTimer& gt)
{
	imguiID = 0;
	OnKeyboardInput(gt);

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

	

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
	UpdateLightCBs(gt);
	UpdateTAA(gt);
	ImguiUpdate();
}

void CGLAB::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());

	// Сбрасываем список команд, используя PSO для теней
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["shadow_opaque"].Get()));
	mCommandList->SetGraphicsRootSignature(mShadowsRootSignature.Get());
	// Устанавливаем кучу дескрипторов (один раз за кадр)
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// Получаем адрес буфера материалов (он нужен почти везде)
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();

	// ==========================================
	// 1. Shadow Pass
	// ==========================================

	// !!! ИСПРАВЛЕНИЕ 1: Привязываем Material Buffer (Slot 2) !!!
	// Без этого шейдер читает мусор и GPU виснет
	mCommandList->SetGraphicsRootShaderResourceView(3, matBuffer->GetGPUVirtualAddress());


	// Выполняем Shadow Pass
	DrawSceneToShadowMap();

	// ==========================================
	// 2. Geometry Pass (G-Buffer)
	// ==========================================

	// Переводим ресурсы GBuffer из GENERIC_READ в RENDER_TARGET
	D3D12_RESOURCE_BARRIER barriers[3];
	for (int i = 0; i < 3; ++i)
		barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(mGBuffer->Resource(i),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(3, barriers);

	// Чистим G-Buffer и ставим его как цель
	float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandles[4];
	for (int i = 0; i < 3; ++i) {
		rtvHandles[i] = mGBuffer->Rtv(i);
		mCommandList->ClearRenderTargetView(rtvHandles[i], clearColor, 0, nullptr);
	}
	mCommandList->ClearRenderTargetView(mVelocityTexture->Rtv(), Colors::Black, 0, nullptr);
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mVelocityTexture->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
	rtvHandles[3] = mVelocityTexture->Rtv();

	// Используем основной Depth Buffer сцены
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(4, rtvHandles, false, &DepthStencilView());
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->SetPipelineState(mPSOs["GeometryPass"].Get());
	mCommandList->SetGraphicsRootSignature(mGeometryRootSignature.Get());

	// Биндим Pass CBV (Slot 1)
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	// Биндим Material Buffer (Slot 2)
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Биндим текстуры (Slot 3 для GeometryRootSignature)
	mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	// Отрисовка непрозрачной геометрии
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	// Переводим GBuffer обратно в GENERIC_READ для Light Pass
	for (int i = 0; i < 3; ++i)
		barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(mGBuffer->Resource(i),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
	mCommandList->ResourceBarrier(3, barriers);

	// ==========================================
	// 3. Lighting Pass
	// ==========================================

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mJitteredTexture->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
	rtvs[0] = CurrentBackBufferView();
	rtvs[1] = mJitteredTexture->Rtv();


	mCommandList->OMSetRenderTargets(2, rtvs, false, nullptr); // Без depth buffer!
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);
	mCommandList->ClearRenderTargetView(rtvs[1], Colors::Black, 0, nullptr);

	mCommandList->SetGraphicsRootSignature(mLightingRootSignature.Get());

	// 0: PassCB
	mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());

	// 0.1: Light CB
	auto lightCB = mCurrFrameResource->LightCB->Resource();
	UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(LightConstants));


	CD3DX12_GPU_DESCRIPTOR_HANDLE skyCubeMap(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyCubeMap.Offset(mResourceMgr->TexOffsets["skyCubeMap"], mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(2, skyCubeMap);



	// 2: GBuffer Table
	mCommandList->SetGraphicsRootDescriptorTable(4, mGBuffer->Srv());

	for (auto& light : mLights)
	{
		auto lightCB = mCurrFrameResource->LightCB->Resource();
		mCommandList->IASetVertexBuffers(0, 1, &mGeomMgr->mGeometries["shapeGeo"]->VertexBufferView());
		mCommandList->IASetIndexBuffer(&mGeomMgr->mGeometries["shapeGeo"]->IndexBufferView());

		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + light->LightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(1, lightCBAddress); // b1
		if (light->type == 2 || light->type == 3)
			mCommandList->SetGraphicsRootDescriptorTable(3, light->ShadowMap->Srv()); // because only directional and spot lights have shadows
		// if directional or ambient -> rendering full screen quad
		if (light->type == 0 || light->type == 2)
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
	// ==========================================
	// 4. Skybox Pass
	// ==========================================

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs_sky[3];
	rtvs_sky[0] = CurrentBackBufferView();
	rtvs_sky[1] = mJitteredTexture->Rtv();
	rtvs_sky[2] = mVelocityTexture->Rtv();
	mCommandList->OMSetRenderTargets(3, rtvs_sky, false, &DepthStencilView());
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get()); // Возвращаем Main Signature
	mCommandList->SetPipelineState(mPSOs["sky"].Get());

	// Биндинг PassCB (Slot 1)
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	// !!! ИСПРАВЛЕНИЕ 3: Привязываем Material Buffer (Slot 2) и для Skybox !!!
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Биндинг CubeMap (Slot 3)
	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mResourceMgr->TexOffsets["skyCubeMap"], mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	// Биндинг Textures (Slot 4) - формально нужен для сигнатуры, даже если скайбокс не использует
	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mVelocityTexture->Resource(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
	// 0: PassCB
	mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());

	//// drawing light shapes
	//mCommandList->OMSetRenderTargets(2, rtvs, false, nullptr); // Без depth buffer!

	//mCommandList->SetPipelineState(mPSOs["lightingShapes"].Get());
	//for (auto& light : mLights)
	//{
	//	if (light->type != 0 && light->type != 2 && light->isDebugOn == 1)
	//	{
	//		auto lightCB = mCurrFrameResource->LightCB->Resource();
	//		mCommandList->IASetVertexBuffers(0, 1, &mGeomMgr->mGeometries["shapeGeo"]->VertexBufferView());
	//		mCommandList->IASetIndexBuffer(&mGeomMgr->mGeometries["shapeGeo"]->IndexBufferView());

	//		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + light->LightCBIndex * lightCBByteSize;
	//		mCommandList->SetGraphicsRootConstantBufferView(1, lightCBAddress);

	//		mCommandList->DrawIndexedInstanced(light->ShapeGeo.IndexCount, 1, light->ShapeGeo.StartIndexLocation, light->ShapeGeo.BaseVertexLocation, 0);
	//	}

	//}



	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mJitteredTexture->Resource(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
	if (useTaa)
	{
		mCommandList->SetPipelineState(mPSOs["TAA"].Get());
		mCommandList->SetGraphicsRootSignature(mTAARootSignature.Get());
		mCommandList->SetGraphicsRootDescriptorTable(1, mJitteredTexture->Srv());

		if (frameIndex % 2 == 0)
		{
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mPrevTexture->Resource(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCurrentTexture->Resource(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
			rtvs[1] = mCurrentTexture->Rtv();
			mCommandList->SetGraphicsRootDescriptorTable(0, mPrevTexture->Srv());
		}
		else
		{
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCurrentTexture->Resource(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mPrevTexture->Resource(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
			rtvs[1] = mPrevTexture->Rtv();
			mCommandList->SetGraphicsRootDescriptorTable(0, mCurrentTexture->Srv());
		}
		mCommandList->SetGraphicsRootDescriptorTable(3, mVelocityTexture->Srv());
		mCommandList->OMSetRenderTargets(2, rtvs, false, nullptr);

		mCommandList->SetGraphicsRootConstantBufferView(2, mCurrFrameResource->TAACB->Resource()->GetGPUVirtualAddress());
		mCommandList->DrawInstanced(3, 1, 0, 0);
	}

	// ==========================================
	// 6. IMGUI Render
	// ==========================================

	ID3D12DescriptorHeap* imguiheaps[] = { m_ImGuiSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(imguiheaps), imguiheaps);
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	// ==========================================
	// 7. Final Presentation
	// ==========================================

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(1, 0));

	frameIndex++;
	frameIndex = frameIndex % 16;

	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}


/*
INPUT HANDLING
*/
void CGLAB::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void CGLAB::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void CGLAB::OnMouseMove(WPARAM btnState, int x, int y)
{
	if (!ImGui::GetIO().WantCaptureMouse)
	{
		if ((btnState & MK_LBUTTON) != 0)
		{
			// Make each pixel correspond to a quarter of a degree.
			float dx = XMConvertToRadians(mConfig.CameraRotationSpeed * static_cast<float>(x - mLastMousePos.x));
			float dy = XMConvertToRadians(mConfig.CameraRotationSpeed * static_cast<float>(y - mLastMousePos.y));

			mCamera.Pitch(dy);
			mCamera.RotateY(dx);
		}

		mLastMousePos.x = x;
		mLastMousePos.y = y;
	}

}

void CGLAB::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(mConfig.CameraWalkSpeed * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-mConfig.CameraWalkSpeed * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-mConfig.CameraWalkSpeed * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(mConfig.CameraWalkSpeed * dt);

	mCamera.UpdateViewMatrix();
}


/*
UPDATE FUNCTIONS
*/
void CGLAB::AnimateMaterials(const GameTimer& gt)
{

}
// Object variables
void CGLAB::UpdateObjectCBs(const GameTimer& gt)
{

	for (auto& rItem : mAllRitems)
	{
		if (rItem->name == "skull")
		{
			count++;
			if (count > 300)
			{
				tr = -tr;
				count = 0;
			}
			XMMATRIX trans = XMMatrixTranslation(tr, 0, 0);
			XMMATRIX world = XMLoadFloat4x4(&rItem->World);
			XMStoreFloat4x4(&rItem->PrevWorld, world);
			world = trans * world;
			XMStoreFloat4x4(&rItem->World, world);
			rItem->NumFramesDirty = gNumFrameResources;
		}
		else if (rItem->name == "box")
		{
			if (count > 300)
			{
				tr = -tr;
				count = 0;
			}
			XMMATRIX trans = XMMatrixTranslation(0, tr, 0);
			XMMATRIX world = XMLoadFloat4x4(&rItem->World);
			XMStoreFloat4x4(&rItem->PrevWorld, world);
			world = trans * world;
			XMStoreFloat4x4(&rItem->World, world);
			rItem->NumFramesDirty = gNumFrameResources;
		}
		else
		{
			XMMATRIX world = XMLoadFloat4x4(&rItem->World);
			XMStoreFloat4x4(&rItem->PrevWorld, world);
		}
	}

	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX prevworld = XMLoadFloat4x4(&e->PrevWorld);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(prevworld));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}
// Main pass variables(gWorld, gView, gProj, etc)
void CGLAB::UpdateMainPassCB(const GameTimer& gt)
{

	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	XMMATRIX proj_jittered = proj;
	if (useTaa)
	{
		proj_jittered.r[2].m128_f32[0] += jitters[frameIndex].x;
		proj_jittered.r[2].m128_f32[1] += jitters[frameIndex].y;
	}

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);
	XMMATRIX viewProj_jittered = XMMatrixMultiply(view, proj_jittered);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	mMainPassCB.PrevViewProj = mMainPassCB.ViewProj;
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.JitteredViewProj, XMMatrixTranspose(viewProj_jittered));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0, 0, 0, 1.0f };
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void CGLAB::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for (auto& e : mResourceMgr->mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void CGLAB::UpdateLightCBs(const GameTimer& gt)
{

	auto currLightCB = mCurrFrameResource->LightCB.get();
	auto currShadowCB = mCurrFrameResource->ShadowCB.get();
	int lId = 0;
	for (auto& l : mLights)
	{
		LightConstants lConst;
		ShadowConstants sConst;
		if (l->type == 1) // Point Light
		{
			XMStoreFloat4x4(&l->gWorld, XMMatrixTranspose(XMMatrixScaling(l->FalloffEnd * 2, l->FalloffEnd * 2, l->FalloffEnd * 2) * XMMatrixTranslation(l->Position.x, l->Position.y, l->Position.z)));
		}
		if (l->type == 3) // Spot Light
		{
			XMStoreFloat4x4(&l->gWorld, XMMatrixTranspose(XMMatrixScaling(l->FalloffEnd * 4 / 3, l->FalloffEnd, l->FalloffEnd * 4 / 3) * XMMatrixTranslation(0, -l->FalloffEnd / 2, 0) *
				XMMatrixRotationRollPitchYaw(XMConvertToRadians(l->Rotation.x), XMConvertToRadians(l->Rotation.y), XMConvertToRadians(l->Rotation.z)) *
				XMMatrixTranslation(l->Position.x, l->Position.y, l->Position.z)));
			XMFLOAT3 d(0, -1, 0);
			XMVECTOR v = XMLoadFloat3(&d);

			v = XMVector3TransformNormal(v, XMMatrixRotationRollPitchYaw(XMConvertToRadians(l->Rotation.x), XMConvertToRadians(l->Rotation.y), XMConvertToRadians(l->Rotation.z)));

			XMStoreFloat3(&l->Direction, v);
			d = XMFLOAT3(-1, 0, 0);
			v = XMLoadFloat3(&d);
			v = XMVector3TransformNormal(v, XMMatrixRotationRollPitchYaw(XMConvertToRadians(l->Rotation.x), XMConvertToRadians(l->Rotation.y), XMConvertToRadians(l->Rotation.z)));
			l->LightUp = v;
			if (l->CastsShadows)
			{
				XMFLOAT3 Pos(l->Position);
				XMVECTOR lightPos = XMLoadFloat3(&Pos);
				XMVECTOR lightDir = XMLoadFloat3(&l->Direction);
				XMVECTOR targetPos = lightPos + lightDir;
				XMVECTOR lightUp = l->LightUp;

				XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
				XMStoreFloat4x4(&l->LightView, lightView);

				XMMATRIX lightProj = XMMatrixPerspectiveFovLH(0.5f * MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
				XMStoreFloat4x4(&l->LightProj, lightProj);
				XMStoreFloat4x4(&l->LightViewProj, XMMatrixTranspose(XMMatrixMultiply(lightView, lightProj)));
			}
		}
		else if (l->type == 2 && l->CastsShadows) // Directional Light
		{
			// Only the first "main" light casts a shadow.
			XMVECTOR lightDir = XMLoadFloat3(&l->Direction);
			XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
			XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
			XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);


			// Transform bounding sphere to light space.
			XMFLOAT3 sphereCenterLS;
			XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

			// Ortho frustum in light space encloses scene.
			float le = sphereCenterLS.x - mSceneBounds.Radius;
			float b = sphereCenterLS.y - mSceneBounds.Radius;
			float n = sphereCenterLS.z - mSceneBounds.Radius;
			float r = sphereCenterLS.x + mSceneBounds.Radius;
			float t = sphereCenterLS.y + mSceneBounds.Radius;
			float f = sphereCenterLS.z + mSceneBounds.Radius;

			XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(le, r, b, t, n, f);

			XMStoreFloat4x4(&l->LightView, lightView);
			XMStoreFloat4x4(&l->LightProj, lightProj);
			XMStoreFloat4x4(&l->LightViewProj, XMMatrixTranspose(XMMatrixMultiply(lightView, lightProj)));
		}

		// Fill in the light constant buffer.
		lConst.Color = l->Color;
		lConst.FalloffStart = l->FalloffStart;
		lConst.Direction = l->Direction;
		lConst.FalloffEnd = l->FalloffEnd;
		lConst.Position = l->Position;
		lConst.SpotPower = l->SpotPower;
		lConst.type = l->type;
		lConst.Strength = l->Strength;
		lConst.CastsShadows = l->CastsShadows;
		lConst.isDebugOn = l->isDebugOn;
		lConst.gWorld = l->gWorld;
		lConst.LightViewProj = l->LightViewProj;
		lConst.enablePCF = l->enablePCF;
		lConst.pcf_level = l->pcf_level;
		// Fill in the shadow constant buffer.
		sConst.LightViewProj = l->LightViewProj;
		currShadowCB->CopyData(l->LightCBIndex, sConst);
		currLightCB->CopyData(l->LightCBIndex, lConst);
		lId++;
	}
}

void CGLAB::UpdateTAA(const GameTimer& gt)
{
	auto TaaCB = mCurrFrameResource->TAACB.get();
	TaaCB->CopyData(0, mTAAConstants);
}

void CGLAB::ImguiUpdate()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGui::Begin("Settings");
	if (ImGui::BeginTabBar("Light Settings"))
	{
		if (ImGui::BeginTabItem("Lights"))
		{
			int lId = 0;
			for (auto& l : mLights)
			{
				if (l->type == 0)
				{
					std::string s = "\nAmbient Light " + std::to_string(lId);
					ImGui::PushID(++imguiID);
					ImGui::Text(s.c_str());
					float* a[] = { &l->Position.x,&l->Position.y,&l->Position.z };
					ImGui::ColorEdit3("Color", (float*)&l->Color);
					ImGui::DragFloat("Strength", &l->Strength, 0.1f, 0, 100);

					ImGui::PopID();
					ImGui::Separator();
				}
				else if (l->type == 1)
				{
					std::string s = "\nPoint Light " + std::to_string(lId);
					ImGui::PushID(++imguiID);
					ImGui::Text(s.c_str());
					float* a[] = { &l->Position.x,&l->Position.y,&l->Position.z };
					ImGui::DragFloat3("Position", *a, 0.1f, -100, 100);
					ImGui::ColorEdit3("Color", (float*)&l->Color);
					ImGui::DragFloat("Strength", &l->Strength, 0.1f, 0, 100);
					ImGui::DragFloat("FaloffStart", &l->FalloffStart, 0.1f, 1, l->FalloffEnd);
					ImGui::DragFloat("FaloffEnd", &l->FalloffEnd, 0.1f, l->FalloffStart, 100);
					bool b = l->isDebugOn;
					ImGui::Checkbox("is Debug On", &b);
					l->isDebugOn = b;
					ImGui::PopID();
					ImGui::Separator();
				}
				else if (l->type == 2)
				{
					std::string s = "\nDirectional Light " + std::to_string(lId);
					ImGui::PushID(++imguiID);
					ImGui::Text(s.c_str());
					ImGui::SliderFloat3("Direction", (float*)&l->Direction, -1, 1);
					ImGui::ColorEdit3("Color", (float*)&l->Color);
					ImGui::DragFloat("Strength", &l->Strength, 0.1f, 0, 100);
					bool b = l->CastsShadows;
					ImGui::Checkbox("Cast Shadows", &b);
					l->CastsShadows = b;
					bool c = l->enablePCF;
					ImGui::Checkbox("Enable PCF", &c);
					l->enablePCF = c;
					ImGui::DragInt("PCF level", &l->pcf_level, 1, 0, 100);
					ImGui::PopID();
					ImGui::Separator();

				}
				else if (l->type == 3)
				{
					std::string s = "\nSpot Light " + std::to_string(lId);
					ImGui::PushID(++imguiID);
					ImGui::Text(s.c_str());
					float* a[] = { &l->Position.x,&l->Position.y,&l->Position.z };
					ImGui::DragFloat3("Position", (float*)&l->Position, 0.1f, -100, 100);
					ImGui::DragFloat3("Rotation", (float*)&l->Rotation, 0.1f, -180, 180);
					ImGui::ColorEdit3("Color", (float*)&l->Color);
					ImGui::DragFloat("Strength", &l->Strength, 0.1f, 0, 100);
					ImGui::DragFloat("Faloff Start", &l->FalloffStart, 0.1f, 0, 100);
					ImGui::DragFloat("Faloff End", &l->FalloffEnd, 0.1f, 0, 100);
					ImGui::SliderFloat("Spot Power", &l->SpotPower, 0, 10);
					ImGui::DragInt("PCF level", &l->pcf_level, 1, 0, 100);
					bool c = l->enablePCF;
					ImGui::Checkbox("Enable PCF", &c);
					l->enablePCF = c;
					bool b = l->CastsShadows;
					ImGui::Checkbox("Cast Shadows", &b);
					l->CastsShadows = b;
					b = l->isDebugOn;
					ImGui::Checkbox("is Debug On", &b);
					l->isDebugOn = b;
					ImGui::PopID();
					ImGui::Separator();

				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("TAA"))
		{
			ImGui::DragFloat("Blendfactor", &mTAAConstants.blendFactor, 0.01f, 0.0f, 1.0f);
			ImGui::Checkbox("Use TAA?", &useTaa);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}


/*
RESOURCE STUFF (descriptor heaps, etc.)
*/
void CGLAB::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 50 + 3 + 3 + 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	// srv for textures
	int mTextureCount = 0;
	for (const auto& tex : mResourceMgr->mTextures)
	{
		if (tex.first == "skyCubeMap")
			continue;
		else
		{
			auto text = tex.second->Resource;
			srvDesc.Format = text->GetDesc().Format;
			srvDesc.Texture2D.MipLevels = text->GetDesc().MipLevels;
			md3dDevice->CreateShaderResourceView(text.Get(), &srvDesc, hDescriptor);
			mResourceMgr->TexOffsets[tex.first] = mTextureCount;
			mTextureCount++;
			// next descriptor
			hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		}
	}

	// srv for cube map
	if (mResourceMgr->mTextures.size() != 0)
	{
		auto skyCubeMap = mResourceMgr->mTextures["skyCubeMap"]->Resource;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
		srvDesc.Format = skyCubeMap->GetDesc().Format;
		mResourceMgr->TexOffsets["skyCubeMap"] = mTextureCount;
		md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);
	}

	auto srvCpuStart = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto srvGpuStart = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto dsvCpuStart = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

	// null cubetex descriptor
	mResourceMgr->TexOffsets["nullCube"] = mTextureCount + 1;
	auto nullSrv = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mResourceMgr->TexOffsets["nullCube"], mCbvSrvUavDescriptorSize);
	mNullSrv = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mResourceMgr->TexOffsets["nullCube"], mCbvSrvUavDescriptorSize);
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);

	// null texture2D descriptor
	mResourceMgr->TexOffsets["nullTex"] = mTextureCount + 2;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);

	// shadow map descriptors
	int shadowsrvOffset = mResourceMgr->TexOffsets["nullCube"] + 2;
	int i = 1;
	for (auto& light : mLights)
	{

		if (light->type == 2 || light->type == 3)
		{
			light->ShadowMap->BuildDescriptors(
				CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, shadowsrvOffset, mCbvSrvUavDescriptorSize),
				CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, shadowsrvOffset, mCbvSrvUavDescriptorSize),
				CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, i + 1, mDsvDescriptorSize));

			i++;
			shadowsrvOffset++;
		}
	}
	gBufferSrvOffset = 50; // last three descriptors are reserved for GBuffer
}

void CGLAB::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +6 RTV for cube render target.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3 + 3 + 1; // +3 for GBuffer RTVs + 3 for prev/current/jittered  frame RTV + 2 for velocity texture
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	// Add +1 DSV for shadow map.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 20 + 1; // MAX 20 lights +1 shadow map
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void CGLAB::BuildGBuffer()
{
	mGBuffer = std::make_unique<GBuffer>(md3dDevice.Get(), mClientWidth, mClientHeight);

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHeapHandle(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srvHeapHandle.Offset(gBufferSrvOffset, mCbvSrvUavDescriptorSize);

	// 2. Получаем GPU хендл (для биндинга в шейдер) и ТОЖЕ оборачиваем в CD3DX12
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gpuSrvHandle.Offset(gBufferSrvOffset, mCbvSrvUavDescriptorSize); // Теперь Offset работает!

	// 3. RTV хендл тоже оборачиваем
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtvHeapHandle.Offset(SwapChainBufferCount, mRtvDescriptorSize);

	// 4. Вызываем функцию (она теперь примет аргументы, так как типы совпадают)
	mGBuffer->BuildDescriptors(
		srvHeapHandle,
		gpuSrvHandle,
		rtvHeapHandle,
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize
	);
}
void CGLAB::BuildTAATextures()
{

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHeapHandle(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srvHeapHandle.Offset(gBufferSrvOffset + 3, mCbvSrvUavDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gpuSrvHandle.Offset(gBufferSrvOffset + 3, mCbvSrvUavDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtvHeapHandle.Offset(5, mRtvDescriptorSize);
	mPrevTexture->BuildDescriptors(
		srvHeapHandle,
		gpuSrvHandle,
		rtvHeapHandle,
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize
	);

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHeapHandle1(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srvHeapHandle1.Offset(gBufferSrvOffset + 4, mCbvSrvUavDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle1(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gpuSrvHandle1.Offset(gBufferSrvOffset + 4, mCbvSrvUavDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle1(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtvHeapHandle1.Offset(6, mRtvDescriptorSize);
	mCurrentTexture->BuildDescriptors(
		srvHeapHandle1,
		gpuSrvHandle1,
		rtvHeapHandle1,
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize
	);


	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHeapHandle2(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srvHeapHandle2.Offset(gBufferSrvOffset + 5, mCbvSrvUavDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle2(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gpuSrvHandle2.Offset(gBufferSrvOffset + 5, mCbvSrvUavDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle2(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtvHeapHandle2.Offset(7, mRtvDescriptorSize);
	mJitteredTexture->BuildDescriptors(
		srvHeapHandle2,
		gpuSrvHandle2,
		rtvHeapHandle2,
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize
	);

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHeapHandle4(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srvHeapHandle4.Offset(gBufferSrvOffset + 6, mCbvSrvUavDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle4(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gpuSrvHandle4.Offset(gBufferSrvOffset + 6, mCbvSrvUavDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle4(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtvHeapHandle4.Offset(8, mRtvDescriptorSize);
	mVelocityTexture->BuildDescriptors(
		srvHeapHandle4,
		gpuSrvHandle4,
		rtvHeapHandle4,
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize
	);
}
void CGLAB::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["shadowAlphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["debugVS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["GPassVS"] = d3dUtil::CompileShader(L"Shaders\\GeometryPass.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["GPassPS"] = d3dUtil::CompileShader(L"Shaders\\GeometryPass.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["lightingVS"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["lightingPS"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["lightingPSDebug"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "PS_debug", "ps_5_1");
	mShaders["lightingQUADVS"] = d3dUtil::CompileShader(L"Shaders\\LightingPass.hlsl", nullptr, "VS_QUAD", "vs_5_1");

	mShaders["TaaVS"] = d3dUtil::CompileShader(L"Shaders\\TAA.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["TaaPS"] = d3dUtil::CompileShader(L"Shaders\\TAA.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void CGLAB::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	//
	// PSO for shadow map pass.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = opaquePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 100000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	smapPsoDesc.pRootSignature = mShadowsRootSignature.Get();
	smapPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
		mShaders["shadowVS"]->GetBufferSize()
	};
	smapPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
		mShaders["shadowOpaquePS"]->GetBufferSize()
	};

	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN; // no render target
	smapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

	//
	// PSO for debug layer.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = opaquePsoDesc;
	debugPsoDesc.pRootSignature = mRootSignature.Get();
	debugPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
		mShaders["debugVS"]->GetBufferSize()
	};
	debugPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
		mShaders["debugPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

	//
	// PSO for sky.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyPsoDesc.DepthStencilState.DepthEnable = true;
	skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = mRootSignature.Get();
	skyPsoDesc.NumRenderTargets = 3;
	skyPsoDesc.RTVFormats[0] = mBackBufferFormat;
	skyPsoDesc.RTVFormats[1] = mPrevTexture->mFormat;
	skyPsoDesc.RTVFormats[2] = mPrevTexture->mFormat;
	skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	//
	// PSO for Geometry pass.
	//
	DXGI_FORMAT GBUFFER_FORMATS[3] = {
		DXGI_FORMAT_R8G8B8A8_UNORM,      // RT0: Albedo + Roughness
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // RT1: Normal + Fresnel
		DXGI_FORMAT_R32G32B32A32_FLOAT   // RT2: Position
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gPassPsoDesc = opaquePsoDesc;

	gPassPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["GPassVS"]->GetBufferPointer()), mShaders["GPassVS"]->GetBufferSize() };
	gPassPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["GPassPS"]->GetBufferPointer()), mShaders["GPassPS"]->GetBufferSize() };

	gPassPsoDesc.NumRenderTargets = 4;
	gPassPsoDesc.RTVFormats[0] = GBUFFER_FORMATS[0];
	gPassPsoDesc.RTVFormats[1] = GBUFFER_FORMATS[1];
	gPassPsoDesc.RTVFormats[2] = GBUFFER_FORMATS[2];
	gPassPsoDesc.RTVFormats[3] = mVelocityTexture->mFormat;

	gPassPsoDesc.DepthStencilState.DepthEnable = true;
	gPassPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	gPassPsoDesc.pRootSignature = mGeometryRootSignature.Get();

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&gPassPsoDesc, IID_PPV_ARGS(&mPSOs["GeometryPass"])));

	//
	// PSO for Lighting pass.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightPsoDesc = {};
	lightPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	lightPsoDesc.pRootSignature = mLightingRootSignature.Get();
	lightPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["lightingVS"]->GetBufferPointer()),
						mShaders["lightingVS"]->GetBufferSize() };
	lightPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["lightingPS"]->GetBufferPointer()),
						mShaders["lightingPS"]->GetBufferSize() };
	lightPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	lightPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;

	D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc = {};
	rtBlendDesc.BlendEnable = TRUE;
	rtBlendDesc.LogicOpEnable = FALSE;
	rtBlendDesc.SrcBlend = D3D12_BLEND_ONE;
	rtBlendDesc.DestBlend = D3D12_BLEND_ONE;
	rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ONE;
	rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rtBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0] = rtBlendDesc;
	lightPsoDesc.BlendState = blendDesc;
	lightPsoDesc.SampleMask = UINT_MAX;
	lightPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	lightPsoDesc.NumRenderTargets = 2;
	lightPsoDesc.RTVFormats[0] = mBackBufferFormat;
	lightPsoDesc.RTVFormats[1] = mPrevTexture->mFormat;
	lightPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	lightPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	lightPsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&lightPsoDesc, IID_PPV_ARGS(&mPSOs["lighting"])));

	//
	// PSO for Lighting pass(QUAD).
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightQUADPsoDesc = lightPsoDesc;
	lightQUADPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	lightQUADPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["lightingQUADVS"]->GetBufferPointer()),
						mShaders["lightingQUADVS"]->GetBufferSize() };

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&lightQUADPsoDesc, IID_PPV_ARGS(&mPSOs["lightingQUAD"])));

	//
	// PSO for Lighting pass(Debug shape draw).
	//
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

	D3D12_GRAPHICS_PIPELINE_STATE_DESC taaPsoDesc = skyPsoDesc;

	taaPsoDesc.DepthStencilState.DepthEnable = false;
	taaPsoDesc.pRootSignature = mTAARootSignature.Get();
	taaPsoDesc.NumRenderTargets = 2;
	taaPsoDesc.RTVFormats[0] = mBackBufferFormat;
	taaPsoDesc.RTVFormats[1] = mPrevTexture->mFormat;
	taaPsoDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
	taaPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["TaaVS"]->GetBufferPointer()),
		mShaders["TaaVS"]->GetBufferSize()
	};
	taaPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["TaaPS"]->GetBufferPointer()),
		mShaders["TaaPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&taaPsoDesc, IID_PPV_ARGS(&mPSOs["TAA"])));

}

void CGLAB::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			2, (UINT)mAllRitems.size(), (UINT)mResourceMgr->mMaterials.size(), (UINT)mLights.size()));
	}
}


/*
ROOT SIGNATURES
*/
void CGLAB::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, -1, 2, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);


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

void CGLAB::BuildShadowsRootSignature()
{
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);
	slotRootParameter[3].InitAsShaderResourceView(0, 1);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
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
		IID_PPV_ARGS(mShadowsRootSignature.GetAddressOf())));
}

void CGLAB::BuildGeometryRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, -1, 0, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);


	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
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
		IID_PPV_ARGS(mGeometryRootSignature.GetAddressOf())));
}

void CGLAB::BuildLightingRootSignature()
{

	CD3DX12_DESCRIPTOR_RANGE gBufferTable;
	gBufferTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 2);

	CD3DX12_DESCRIPTOR_RANGE cubeMap;
	cubeMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE shadowMap;
	shadowMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsConstantBufferView(0); // pass CB
	slotRootParameter[1].InitAsConstantBufferView(1); // light pass CB
	slotRootParameter[2].InitAsDescriptorTable(1, &cubeMap);
	slotRootParameter[3].InitAsDescriptorTable(1, &shadowMap);
	slotRootParameter[4].InitAsDescriptorTable(1, &gBufferTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)GetStaticSamplers().size(), GetStaticSamplers().data(),
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
		IID_PPV_ARGS(mLightingRootSignature.GetAddressOf())));
}

void CGLAB::BuildTAARootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE prevFrame;
	prevFrame.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE currentFrame;
	currentFrame.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	CD3DX12_DESCRIPTOR_RANGE positionOld;
	positionOld.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
	CD3DX12_DESCRIPTOR_RANGE positionNew;
	positionNew.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsDescriptorTable(1, &prevFrame);
	slotRootParameter[1].InitAsDescriptorTable(1, &currentFrame);
	slotRootParameter[2].InitAsConstantBufferView(0); // TAA CB
	slotRootParameter[3].InitAsDescriptorTable(1, &positionOld);
	slotRootParameter[4].InitAsDescriptorTable(1, &positionNew);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)GetStaticSamplers().size(), GetStaticSamplers().data(),
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
		IID_PPV_ARGS(mTAARootSignature.GetAddressOf())));
}


/*
LIGHT INITIALIZATION
*/
void CGLAB::CreatePointLight(XMFLOAT3 pos, XMFLOAT3 color, float faloff_start, float faloff_end, float strength)
{
	std::unique_ptr<Light>light = std::make_unique<Light>();
	light->LightCBIndex = static_cast<int>(mLights.size());

	light->Position = pos;
	light->Color = color;
	light->FalloffStart = faloff_start;
	light->FalloffEnd = faloff_end;
	light->type = 1;
	auto& world = XMMatrixScaling(faloff_end * 2, faloff_end * 2, faloff_end * 2) * XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMStoreFloat4x4(&light->gWorld, XMMatrixTranspose(world));
	mLights.push_back(std::move(light));
}

void CGLAB::CreateSpotLight(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 color, float faloff_start, float faloff_end, float strength, float spotpower)
{
	std::unique_ptr<Light>light = std::make_unique<Light>();
	light->LightCBIndex = static_cast<int>(mLights.size());

	light->Position = pos;
	light->Color = color;
	light->FalloffStart = faloff_start;
	light->FalloffEnd = faloff_end;
	light->Rotation = rot;
	light->LightUp = XMVectorSet(0, 1, 0, 0);
	light->type = 3;
	light->Strength = strength;
	light->SpotPower = spotpower;
	light->ShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), mConfig.ShadowMapHeight, mConfig.ShadowMapWidth);

	mLights.push_back(std::move(light));
}

void CGLAB::BuildLights()
{
	// ambient
	std::unique_ptr<Light>ambient = std::make_unique<Light>();
	ambient->LightCBIndex = static_cast<int>(mLights.size());
	ambient->Position = { 3.0f, 0.0f, 3.0f };
	ambient->Color = { 1,1,1 }; // need only x
	ambient->Strength = 0.5;
	ambient->type = 0;
	XMStoreFloat4x4(&ambient->gWorld, XMMatrixTranspose(XMMatrixTranslation(0, 0, 0) * XMMatrixScaling(1000, 1000, 1000)));
	mLights.push_back(std::move(ambient));

	// directional
	std::unique_ptr<Light>dir = std::make_unique<Light>();
	dir->LightCBIndex = static_cast<int>(mLights.size());
	dir->Position = { 0,20,0 };
	dir->Direction = { -1, -1, 0 };
	dir->Color = { 1,1,1 };
	dir->Strength = 1;
	dir->type = 2;
	dir->enablePCF = 1;
	dir->LightUp = XMVectorSet(0, 0, 1, 0);
	auto& world = XMMatrixScaling(1, 1, 1);
	dir->ShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), mConfig.ShadowMapHeight, mConfig.ShadowMapWidth);
	XMStoreFloat4x4(&dir->gWorld, XMMatrixTranspose(world));
	mLights.push_back(std::move(dir));

	// other
	CreatePointLight({ -3,3,0 }, { 4,0,0 }, 1, 5, 1);
	CreatePointLight({ 3,3,0 }, { 0,0,4 }, 1, 5, 1);
	CreateSpotLight({ -5,3,30 }, { 0,0,-90 }, { 1,1,1 }, 1, 30, 6, 1);
}

void CGLAB::SetLightShapes()
{
	for (auto& light : mLights)
	{

		switch (light->type)
		{
		case 1:
			light->ShapeGeo = mGeomMgr->mGeometries["shapeGeo"]->DrawArgs["sphere"];
			break;
		case 3:
			light->ShapeGeo = mGeomMgr->mGeometries["shapeGeo"]->DrawArgs["box"];
			break;
		}
	}
}

/*
RENDER ITEMS
*/
void CGLAB::CreateRenderItem(std::string objname, std::string geoname, std::string materialname, int RItemLayer, XMMATRIX& scaling, XMMATRIX& rotation, XMMATRIX& translation, XMMATRIX texTransform, std::string drawargs)
{
	if (drawargs == "")
	{
		for (const auto& drawArg : mGeomMgr->mGeometries[geoname]->DrawArgs)
		{
			auto ritem_child = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&ritem_child->World, scaling * rotation * translation);
			XMStoreFloat4x4(&ritem_child->TexTransform, texTransform);
			ritem_child->name = objname + "_" + drawArg.first;
			ritem_child->ObjCBIndex = mAllRitems.size();
			ritem_child->Mat = mResourceMgr->mMaterials[materialname].get();
			ritem_child->Geo = mGeomMgr->mGeometries[geoname].get();
			ritem_child->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			ritem_child->IndexCount = ritem_child->Geo->DrawArgs[drawargs].IndexCount;
			ritem_child->StartIndexLocation = ritem_child->Geo->DrawArgs[drawargs].StartIndexLocation;
			ritem_child->BaseVertexLocation = ritem_child->Geo->DrawArgs[drawargs].BaseVertexLocation;
			mRitemLayer[RItemLayer].push_back(ritem_child.get());
			mAllRitems.push_back(std::move(ritem_child));
		}
	}
	else
	{
		auto ritem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&ritem->World, scaling * rotation * translation);
		XMStoreFloat4x4(&ritem->TexTransform, texTransform);
		ritem->name = objname;
		ritem->ObjCBIndex = mAllRitems.size();
		ritem->Mat = mResourceMgr->mMaterials[materialname].get();
		ritem->Geo = mGeomMgr->mGeometries[geoname].get();
		ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		ritem->IndexCount = ritem->Geo->DrawArgs[drawargs].IndexCount;
		ritem->StartIndexLocation = ritem->Geo->DrawArgs[drawargs].StartIndexLocation;
		ritem->BaseVertexLocation = ritem->Geo->DrawArgs[drawargs].BaseVertexLocation;
		mRitemLayer[RItemLayer].push_back(ritem.get());
		mAllRitems.push_back(std::move(ritem));
	}
}

void CGLAB::BuildRenderItems()
{
	CreateRenderItem("skybox", "shapeGeo", "sky", (int)RenderLayer::Sky, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f), XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity(), "sphere");
	CreateRenderItem("skull", "skullGeo", "skullMat", (int)RenderLayer::Opaque, XMMatrixScaling(2.0f, 2.0f, 2.0f), XMMatrixIdentity(), XMMatrixTranslation(0.0f, 3.0f, 0.0f), XMMatrixScaling(1.0f, 1.0f, 1.0f), "Group5732");
	CreateRenderItem("debugquad", "shapeGeo", "bricks0", (int)RenderLayer::Debug, XMMatrixScaling(1.0f, 1.0f, 1.0f), XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity(), "quad");
	CreateRenderItem("box", "shapeGeo", "bricks0", (int)RenderLayer::Opaque, XMMatrixScaling(2.0f, 1.0f, 2.0f), XMMatrixIdentity(), XMMatrixTranslation(0.0f, 0.5f, 0.0f), XMMatrixScaling(1.0f, 0.5f, 1.0f), "box");
	CreateRenderItem("floor", "shapeGeo", "tile0", (int)RenderLayer::Opaque, XMMatrixScaling(1.0f, 1.0f, 1.0f), XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixScaling(8.0f, 8.0f, 1.0f), "grid");

	XMMATRIX scaleIdentity = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	XMMATRIX rotIdentity = XMMatrixIdentity();
	XMMATRIX texIdentity = XMMatrixIdentity();
	XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
	int layer = (int)RenderLayer::Opaque;
	std::string geoName = "shapeGeo";

	for (int i = 0; i < 5; ++i)
	{
		XMMATRIX leftCylTranslation = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylTranslation = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX leftSphereTranslation = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereTranslation = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		CreateRenderItem("leftcyl", geoName, "bricks0", layer, scaleIdentity, rotIdentity, leftCylTranslation, brickTexTransform, "cylinder");
		CreateRenderItem("rightcyl", geoName, "bricks0", layer, scaleIdentity, rotIdentity, rightCylTranslation, brickTexTransform, "cylinder");

		CreateRenderItem("leftsphere", geoName, "mirror0", layer, scaleIdentity, rotIdentity, leftSphereTranslation, texIdentity, "sphere");
		CreateRenderItem("rightsphere", geoName, "mirror0", layer, scaleIdentity, rotIdentity, rightSphereTranslation, texIdentity, "sphere");
	}
}

void CGLAB::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void CGLAB::DrawSceneToShadowMap()
{

	for (auto& light : mLights)
	{

		if (light->type == 2 || light->type == 3)
		{
			if (light->CastsShadows)
			{
				mCommandList->RSSetViewports(1, &light->ShadowMap->Viewport());
				mCommandList->RSSetScissorRects(1, &light->ShadowMap->ScissorRect());
				mCommandList->SetGraphicsRootSignature(mShadowsRootSignature.Get());
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(light->ShadowMap->Resource(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

				UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
				UINT shadowCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ShadowConstants));

				// Clear the back buffer and depth buffer.
				mCommandList->ClearDepthStencilView(light->ShadowMap->Dsv(),
					D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

				// Set null render target because we are only going to draw to
				// depth buffer.  Setting a null render target will disable color writes.
				// Note the active PSO also must specify a render target count of 0.
				mCommandList->OMSetRenderTargets(0, nullptr, false, &light->ShadowMap->Dsv());

				// Bind the pass constant buffer for the shadow map pass.
				auto passCB = mCurrFrameResource->PassCB->Resource();
				mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());
				auto shadowCB = mCurrFrameResource->ShadowCB->Resource();
				mCommandList->SetGraphicsRootConstantBufferView(2, shadowCB->GetGPUVirtualAddress() + light->LightCBIndex * shadowCBByteSize);
				mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());

				DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

				// Change back to GENERIC_READ so we can read the texture in a shader.
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(light->ShadowMap->Resource(),
					D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
			}
		}
	}

}

/*
OTHER
*/
std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> CGLAB::GetStaticSamplers()
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

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp,
		shadow
	};
}

// HALTON SEQUENCE
void CGLAB::GenerateTransformedHaltonSequence(float viewSizeX, float viewSizeY, XMFLOAT2* outJitters)
{
	const XMFLOAT2 HaltonPoints[16] =
	{
		XMFLOAT2(0.500000f, 0.333333f),
		XMFLOAT2(0.250000f, 0.666667f),
		XMFLOAT2(0.750000f, 0.111111f),
		XMFLOAT2(0.125000f, 0.444444f),
		XMFLOAT2(0.625000f, 0.777778f),
		XMFLOAT2(0.375000f, 0.222222f),
		XMFLOAT2(0.875000f, 0.555556f),
		XMFLOAT2(0.062500f, 0.888889f),
		XMFLOAT2(0.562500f, 0.037037f),
		XMFLOAT2(0.312500f, 0.370370f),
		XMFLOAT2(0.812500f, 0.703704f),
		XMFLOAT2(0.187500f, 0.148148f),
		XMFLOAT2(0.687500f, 0.481481f),
		XMFLOAT2(0.437500f, 0.814815f),
		XMFLOAT2(0.937500f, 0.259259f),
		XMFLOAT2(0.031250f, 0.592593f)
	};

	for (int i = 0; i < 16; ++i)
	{
		// Calculate the X component
		outJitters[i].x = ((HaltonPoints[i].x - 0.5f) / viewSizeX) * 2.0f;

		// Calculate the Y component
		outJitters[i].y = ((HaltonPoints[i].y - 0.5f) / viewSizeY) * 2.0f;
	}
}


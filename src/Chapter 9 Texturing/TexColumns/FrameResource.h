#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvWorld = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 PrevWorld = MathHelper::Identity4x4();
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View;
    DirectX::XMFLOAT4X4 InvView;
    DirectX::XMFLOAT4X4 Proj;
    DirectX::XMFLOAT4X4 InvProj;
    DirectX::XMFLOAT4X4 ViewProj;
    DirectX::XMFLOAT4X4 InvViewProj;

    DirectX::XMFLOAT3 EyePosW;
    float cbPerObjectPad1;

    DirectX::XMFLOAT2 RenderTargetSize;
    DirectX::XMFLOAT2 InvRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;

    DirectX::XMFLOAT4 AmbientLight;


    DirectX::XMFLOAT4X4 ViewProjNoJitter;
    DirectX::XMFLOAT4X4 PrevViewProjNoJitter;
    DirectX::XMFLOAT4X4 PrevViewProj;
    DirectX::XMFLOAT2   CurrJitterUV;
    DirectX::XMFLOAT2   PrevJitterUV;
    DirectX::XMFLOAT2   _padTaa;

    Light Lights[MaxLights];
};


struct LightConstants
{
    Light light;
};

struct PassShadowConstants
{
    DirectX::XMFLOAT4X4 LightViewProj = MathHelper::Identity4x4();
};

// AtmosphereConstants is defined in ../../Common/d3dUtil.h (included above)

struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 Tangent;
    Vertex(DirectX::XMFLOAT3 _pos, DirectX::XMFLOAT3 _nm, DirectX::XMFLOAT2 _uv, DirectX::XMFLOAT3 _tan);
    Vertex() {};
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount,UINT lightCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
   // std::unique_ptr<UploadBuffer<FrameConstants>> FrameCB = nullptr;
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<LightConstants>> LightCB = nullptr;
    std::unique_ptr<UploadBuffer<PassShadowConstants>> PassShadowCB = nullptr;
    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 Fence = 0;
};


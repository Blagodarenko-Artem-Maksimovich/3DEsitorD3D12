#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    UINT MaterialIndex;
    UINT ObjPad0;
    UINT ObjPad1;
    UINT ObjPad2;
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

    Light Lights[MaxLights];
};

// Atmospheric scattering parameters based on GPU Gems 2 Chapter 16
struct AtmosphereConstants
{
    DirectX::XMFLOAT3 SunDirection = { 0.0f, 1.0f, 0.0f };
    float SunIntensity = 22.0f;
    
    DirectX::XMFLOAT3 RayleighScattering = { 5.8e-6f, 13.5e-6f, 33.1e-6f }; // Wavelength-dependent
    float PlanetRadius = 6371000.0f; // Earth radius in meters
    
    DirectX::XMFLOAT3 MieScattering = { 21e-6f, 21e-6f, 21e-6f }; // Wavelength-independent
    float AtmosphereRadius = 6471000.0f; // Atmosphere top radius
    
    float RayleighScaleHeight = 8500.0f;  // Scale height for Rayleigh scattering
    float MieScaleHeight = 1200.0f;       // Scale height for Mie scattering
    float MieAnisotropy = 0.758f;         // Mie phase function anisotropy (g parameter)
    float AtmosphereDensity = 1.0f;       // Density multiplier (1.0 = clean, >1.0 = dirty/polluted)
    
    DirectX::XMFLOAT3 CameraPositionKm = { 0.0f, 0.0f, 0.0f }; // Camera position in km
    float Exposure = 2.0f;                // HDR exposure
    
    int NumSamples = 16;                  // Number of samples along view ray
    int NumLightSamples = 8;              // Number of samples along light ray
    float pad0;
    float pad1;
};

struct MaterialData
{
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.5f;

    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

    UINT DiffuseMapIndex = 0;
    UINT NormalMapIndex = 0;
    UINT MaterialPad1;
    UINT MaterialPad2;
};

struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 TangentU;
};

struct FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<AtmosphereConstants>> AtmosphereCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;

    UINT64 Fence = 0;
};

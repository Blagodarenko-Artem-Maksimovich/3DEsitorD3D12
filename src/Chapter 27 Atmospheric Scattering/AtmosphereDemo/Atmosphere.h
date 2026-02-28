#pragma once

#include "../../Common/d3dUtil.h"

// Atmospheric Scattering implementation based on:
// - GPU Gems 2, Chapter 16: Accurate Atmospheric Scattering
// - Sean O'Neil's atmospheric scattering model
// 
// This class manages the atmospheric scattering effect with real-time
// adjustable parameters for "clean" or "dirty" atmosphere simulation.

class Atmosphere
{
public:
    Atmosphere(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
    Atmosphere(const Atmosphere& rhs) = delete;
    Atmosphere& operator=(const Atmosphere& rhs) = delete;
    ~Atmosphere() = default;

    ID3D12Resource* Resource() { return mAtmosphereMap.Get(); }
    CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const { return mhGpuSrv; }
    CD3DX12_CPU_DESCRIPTOR_HANDLE Rtv() const { return mhCpuRtv; }

    D3D12_VIEWPORT Viewport() const { return mViewport; }
    D3D12_RECT ScissorRect() const { return mScissorRect; }

    UINT Width() const { return mWidth; }
    UINT Height() const { return mHeight; }

    void BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv);

    void OnResize(UINT newWidth, UINT newHeight);

    // Atmosphere parameters
    struct Parameters
    {
        // Sun parameters
        DirectX::XMFLOAT3 SunDirection = { 0.0f, 0.707f, 0.707f };
        float SunIntensity = 20.0f;

        // Rayleigh scattering coefficients (wavelength-dependent)
        DirectX::XMFLOAT3 RayleighCoefficients = { 5.8e-6f, 13.5e-6f, 33.1e-6f };
        float RayleighScaleHeight = 8500.0f;

        // Mie scattering coefficients (wavelength-independent)
        DirectX::XMFLOAT3 MieCoefficients = { 21e-6f, 21e-6f, 21e-6f };
        float MieScaleHeight = 1200.0f;
        float MieAnisotropy = 0.76f; // g parameter for Henyey-Greenstein phase function

        // Planet parameters
        float PlanetRadius = 6371.0f;     // km
        float AtmosphereHeight = 100.0f;  // km above planet surface

        // Density multiplier: 1.0 = clean atmosphere, >1.0 = dirty/polluted
        float DensityMultiplier = 1.0f;

        // Rendering parameters
        float Exposure = 1.5f;
        int NumViewSamples = 16;
        int NumLightSamples = 8;
    };

    Parameters& GetParameters() { return mParams; }
    const Parameters& GetParameters() const { return mParams; }

    // Preset atmosphere configurations
    void SetCleanAtmosphere();
    void SetDirtyAtmosphere();
    void SetMarsAtmosphere();
    void SetSunsetAtmosphere();

private:
    void BuildResource();

private:
    ID3D12Device* md3dDevice = nullptr;

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;

    UINT mWidth = 0;
    UINT mHeight = 0;
    DXGI_FORMAT mFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuRtv;

    Microsoft::WRL::ComPtr<ID3D12Resource> mAtmosphereMap = nullptr;

    Parameters mParams;
};

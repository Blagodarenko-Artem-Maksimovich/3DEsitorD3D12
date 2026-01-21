#pragma once
#pragma once
#include "d3dUtil.h" // Используем утилиты из книги Луны

class GBuffer
{
public:
    // Мы будем хранить:
    // 0: Diffuse Albedo (RGB) + Roughness (A)
    // 1: Normal (RGB) + Metalness/Fresnel (A)
    // 2: World Position (RGB) + Unused (A)
    static const int BufferCount = 3;

    GBuffer(ID3D12Device* device, UINT width, UINT height);

    void BuildResource();
    void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
        UINT cbvSrvUavDescriptorSize,
        UINT rtvDescriptorSize);

    void OnResize(UINT newWidth, UINT newHeight);

    // Функции для получения ресурсов и дескрипторов
    ID3D12Resource* Resource(int i) const { return mResources[i].Get(); }
    CD3DX12_CPU_DESCRIPTOR_HANDLE Rtv(int i) const { return mRtvCPU[i]; }
    CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const { return mSrvGPU; } // Начало таблицы SRV

private:
    ID3D12Device* md3dDevice = nullptr;
    UINT mWidth = 0;
    UINT mHeight = 0;

    // Форматы наших буферов
    DXGI_FORMAT mFormats[BufferCount] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,      // Albedo + Roughness (достаточно 8 бит)
        DXGI_FORMAT_R16G16B16A16_FLOAT,  // Normal + Fresnel (нужна точность для нормалей)
        DXGI_FORMAT_R32G32B32A32_FLOAT   // Position (нужна высокая точность)
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> mResources[BufferCount];

    // Дескрипторы
    CD3DX12_CPU_DESCRIPTOR_HANDLE mRtvCPU[BufferCount];
    CD3DX12_CPU_DESCRIPTOR_HANDLE mSrvCPU;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mSrvGPU;
};
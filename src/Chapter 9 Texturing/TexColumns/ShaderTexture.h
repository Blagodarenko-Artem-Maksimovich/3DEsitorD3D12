#pragma once
#include "d3dUtil.h" // Используем утилиты из книги Луны

class ShaderTexture
{
public:

    ShaderTexture(ID3D12Device* device, UINT width, UINT height);

    void BuildResource();
    void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
        UINT cbvSrvUavDescriptorSize,
        UINT rtvDescriptorSize);

    void OnResize(UINT newWidth, UINT newHeight);

    // Функции для получения ресурсов и дескрипторов
    ID3D12Resource* Resource() const { return mResource.Get(); }
    CD3DX12_CPU_DESCRIPTOR_HANDLE Rtv() const { return mRtvCPU; }
    CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const { return mSrvGPU; } // Начало таблицы SRV
    // Форматы наших буферов
    DXGI_FORMAT mFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
private:
    ID3D12Device* md3dDevice = nullptr;
    UINT mWidth = 0;
    UINT mHeight = 0;

   
  

    Microsoft::WRL::ComPtr<ID3D12Resource> mResource;

    // Дескрипторы
    CD3DX12_CPU_DESCRIPTOR_HANDLE mRtvCPU;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mSrvCPU;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mSrvGPU;
};
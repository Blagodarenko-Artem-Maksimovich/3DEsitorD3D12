#include "ShaderTexture.h"

ShaderTexture::ShaderTexture(ID3D12Device* device, UINT width, UINT height) :
	md3dDevice(device), mWidth(width), mHeight(height)
{
	BuildResource();
}

void ShaderTexture::BuildResource()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	CD3DX12_CLEAR_VALUE optClear(DXGI_FORMAT_UNKNOWN, clearColor);

	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	optClear.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, // Начальное состояние - чтение
		&optClear,
		IID_PPV_ARGS(&mResource)));
	mResource->SetName(L"Shader Texture Resource");
	////////////////
}

void ShaderTexture::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
    UINT cbvSrvUavDescriptorSize,
    UINT rtvDescriptorSize)
{
    mSrvCPU = hCpuSrv;
    mSrvGPU = hGpuSrv;

    // Создаем RTV (Render Target Views)
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = mFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    mRtvCPU = hCpuRtv;
    md3dDevice->CreateRenderTargetView(mResource.Get(), &rtvDesc, mRtvCPU);

    // Сдвигаем дескриптор
    hCpuRtv.Offset(1, rtvDescriptorSize);

    // Создаем SRV (Shader Resource Views) - чтобы читать их как текстуры
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor = hCpuSrv;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    md3dDevice->CreateShaderResourceView(mResource.Get(), &srvDesc, hDescriptor);
    hDescriptor.Offset(1, cbvSrvUavDescriptorSize);

}

void ShaderTexture::OnResize(UINT newWidth, UINT newHeight)
{
    mWidth = newWidth;
    mHeight = newHeight;
    BuildResource();
}
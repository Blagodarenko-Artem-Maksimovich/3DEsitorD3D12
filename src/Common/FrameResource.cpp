#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT lightcount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    TAACB = std::make_unique<UploadBuffer<TAAConstants>>(device, passCount, true);
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    ShadowCB = std::make_unique<UploadBuffer<ShadowConstants>>(device, lightcount, true);
    LightCB = std::make_unique<UploadBuffer<LightConstants>>(device, lightcount, true);
}

FrameResource::~FrameResource()
{

}
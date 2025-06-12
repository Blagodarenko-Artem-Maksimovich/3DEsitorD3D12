#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT lightCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

  //  FrameCB = std::make_unique<UploadBuffer<FrameConstants>>(device, 1, true);
    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    LightCB = std::make_unique<UploadBuffer<LightConstants>>(device, lightCount, true);
    PassShadowCB = std::make_unique<UploadBuffer<PassShadowConstants>>(device, lightCount, true);
}

FrameResource::~FrameResource()
{

}
Vertex::Vertex(DirectX::XMFLOAT3 _pos, DirectX::XMFLOAT3 _nm, DirectX::XMFLOAT2 _uv, DirectX::XMFLOAT3 _tan)
{
    Pos = _pos;
    Normal = _nm;
    TexC = _uv;
	Tangent = _tan;
}
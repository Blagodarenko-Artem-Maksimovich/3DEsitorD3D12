#pragma once

#include "../../Common/d3dUtil.h"
#include "FrameResource.h"
#include <string>
#include <DirectXMath.h>

using namespace DirectX;

struct RenderItem
{
    RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;

    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMMATRIX ScaleM = XMMatrixIdentity();
    XMMATRIX RotationM = XMMatrixIdentity();
    XMMATRIX TranslationM = XMMatrixIdentity();

    XMFLOAT3 Position = { 0.0f, 0.0f, 2.0f };
    XMFLOAT3 RotationAngle = { 0.0f, .0f, 0.0f };
    XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    int NumFramesDirty = gNumFrameResources;

    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
    std::string Name;
};




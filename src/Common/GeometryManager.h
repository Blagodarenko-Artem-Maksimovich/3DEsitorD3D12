#pragma once
#include "d3dUtil.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;
class GeometryManager
{
public:
	GeometryManager() = default;
	~GeometryManager() = default;
	static void InitAll();
	void BuildBasicGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdlist);
	void BuildSkullGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdlist);
	void BuildGeometryFromFile(ID3D12Device* device, ID3D12GraphicsCommandList* cmdlist, const std::string& filename, const std::string& geometryName);
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
};
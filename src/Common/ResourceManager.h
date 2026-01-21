#pragma once
#include <string>
#include <d3dUtil.h>
using namespace DirectX;
class ResourceManager {
public:
	ResourceManager() = default;
	~ResourceManager() = default;
	void Init(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
		md3dDevice = device;
		mCommandList = cmdList;
	}
	void LoadTextures();
	void CreateMaterial(std::string name, int DiffuseIndex, int NormalIndex, XMFLOAT4 DiffuseAlbedo, XMFLOAT3 FresnelR0, float roughness);
	void BuildMaterials();

    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, int>TexOffsets;

private:
	ID3D12Device* md3dDevice = nullptr;
	ID3D12GraphicsCommandList* mCommandList = nullptr;
};

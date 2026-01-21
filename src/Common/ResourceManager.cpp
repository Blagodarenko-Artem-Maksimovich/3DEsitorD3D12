#include "ResourceManager.h"

void ResourceManager::LoadTextures()
{
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"bricksNormalMap",
		"tileDiffuseMap",
		"tileNormalMap",
		"defaultDiffuseMap",
		"defaultNormalMap",
		"skyCubeMap",
		"skullDiffuse",
		"skullNormal"
	};

	std::vector<std::wstring> texFilenames =
	{
		L"Textures/bricks2.dds",
		L"Textures/bricks2_nmap.dds",
		L"Textures/tile.dds",
		L"Textures/tile_nmap.dds",
		L"Textures/white1x1.dds",
		L"Textures/default_nmap.dds",
		L"Textures/desertcube1024.dds",
		L"Textures/skull.dds",
		L"Textures/skull_nm.dds"
	};

	for (int i = 0; i < (int)texNames.size(); ++i)
	{
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice,
			mCommandList, texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));

		mTextures[texMap->Name] = std::move(texMap);
	}
}

void ResourceManager::CreateMaterial(std::string name, int DiffuseIndex, int NormalIndex, XMFLOAT4 DiffuseAlbedo, XMFLOAT3 FresnelR0, float roughness)
{
	auto mat = std::make_unique<Material>();
	mat->Name = name;
	mat->MatCBIndex = mMaterials.size();
	mat->DiffuseSrvHeapIndex = DiffuseIndex;
	mat->NormalSrvHeapIndex = NormalIndex;
	mat->DiffuseAlbedo = DiffuseAlbedo;
	mat->FresnelR0 = FresnelR0;
	mat->Roughness = roughness;
	mMaterials[name] = std::move(mat);
}

void ResourceManager::BuildMaterials()
{
	CreateMaterial("bricks0", TexOffsets["bricksDiffuseMap"], TexOffsets["bricksNormalMap"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.1f, 0.1f, 0.1f), 0.3f);
	CreateMaterial("tile0", TexOffsets["tileDiffuseMap"], TexOffsets["tileNormalMap"], XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f), XMFLOAT3(0.2f, 0.2f, 0.2f), 0.1f);
	CreateMaterial("mirror0", TexOffsets["defaultDiffuseMap"], TexOffsets["defaultNormalMap"], XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f), XMFLOAT3(0.98f, 0.97f, 0.95f), 0.1f);
	CreateMaterial("skullMat", TexOffsets["skullDiffuse"], TexOffsets["skullNormal"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.f, 0.f, 0.f), 0.7f);
	CreateMaterial("sky", TexOffsets["skyCubeMap"], TexOffsets["shadow"] + 1, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.1f, 0.1f, 0.1f), 1.0f);
}

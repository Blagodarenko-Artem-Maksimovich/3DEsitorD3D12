#pragma once
#include "UploadManager.h"
#include "FrameResource.h"
#include "RenderItem.h"
#include "DirectXMath.h"
#include "TextureManager.h"
#include "../../Common/GBuffer.h"
#include "GeometryManager.h"

class ObjectManager
{
public:
	ObjectManager();
	~ObjectManager();

	virtual int addRenderItem(ID3D12Device* device, const std::wstring& itemName) = 0;
	virtual bool deleteObject(int selectedObject) = 0;

	virtual void UpdateObjectCBs(FrameResource* currFrameResource) = 0;
	virtual void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) = 0;
	virtual int objectsCount() = 0;
	virtual std::string objectName(int i) = 0;
	virtual RenderItem* object(int i) = 0;
	virtual void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource) = 0;
	void Init();

protected:
	std::unordered_map<std::wstring, int> _objectCounters;
	std::unordered_map<std::wstring, int> _objectLoaded;

	std::uint32_t uidCount;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	Microsoft::WRL::ComPtr<ID3DBlob> _vsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _psShader;

	virtual std::wstring _shaderName() = 0;
	virtual D3D12_PRIMITIVE_TOPOLOGY_TYPE _topologyType() = 0;

	virtual void BuildInputLayout() = 0;
	virtual void BuildRootSignature() = 0;
private:
	void BuildShaders();
	void BuildPSO();
};

class OpaqueObjectManager : public ObjectManager
{
public:
	void UpdateObjectCBs(FrameResource* currFrameResource) override;

private:
	std::vector<std::unique_ptr<OpaqueRenderItem>> _objects;

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;
	int addRenderItem(ID3D12Device* device, const std::wstring& itemName) override;
	bool deleteObject(int selectedObject) override;
	int objectsCount() override;
	std::string objectName(int i) override;
	RenderItem* object(int i) override;
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource) override;

	std::wstring _shaderName() override
	{
		return L"GBuffer";
	}

	D3D12_PRIMITIVE_TOPOLOGY_TYPE _topologyType() override
	{
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}
};

class UnlitObjectManager : public ObjectManager
{
public:
	void UpdateObjectCBs(FrameResource* currFrameResource) override;

private:
	std::vector<std::unique_ptr<UnlitRenderItem>> _objects;

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;
	int addRenderItem(ID3D12Device* device, const std::wstring& itemName) override;
	bool deleteObject(int selectedObject) override;
	int objectsCount() override;
	std::string objectName(int i) override;
	RenderItem* object(int i) override;
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource) override;

	std::wstring _shaderName() override
	{
		return L"Unlit";
	}

	D3D12_PRIMITIVE_TOPOLOGY_TYPE _topologyType() override
	{
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	}
};
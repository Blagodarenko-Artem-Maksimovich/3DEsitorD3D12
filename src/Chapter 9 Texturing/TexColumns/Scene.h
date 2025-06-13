#pragma once

#include "FrameResource.h"
#include "../../Common/d3dUtil.h"
#include <unordered_map>
#include <vector>
#include "RenderItem.h"
#include <memory>
#include <string>

class Scene
{
public:
    Scene();
    ~Scene();

    void AddRenderItem(std::unique_ptr<RenderItem> item);
    void AddLight(std::unique_ptr<Light> light);
    void Clear();

    void Update(float dt);
    void Draw(ID3D12GraphicsCommandList* cmdList);
    void MarkDirty();
    
    void RemoveRenderItemsByName(const std::string& name);
    void RemoveLightsByIndex(int index);

    int LightsGetSize();
    int RenderItemsGetSize();

    auto& LightsGetBegine();
    auto& RenderItemsGetBegine();

    RenderItem* GetRenderItemByIndex(int index);
    Light* GetLightByIndex(int index);

    std::vector<std::unique_ptr<RenderItem>>& GetRenderItems();
    std::vector<std::unique_ptr<Light>>& GetLights();

private:
    std::vector<std::unique_ptr<RenderItem>> mRenderItems;
    std::vector <std::unique_ptr<Light>> mLights;
};




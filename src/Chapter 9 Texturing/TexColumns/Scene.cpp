#include "Scene.h"

Scene::Scene() {}

Scene::~Scene()
{
    Clear();
}

void Scene::AddRenderItem(std::unique_ptr<RenderItem> item)
{
    mRenderItems.push_back(std::move(item));
}

void Scene::AddLight(std::unique_ptr<Light> light)
{
    mLights.push_back(std::move(light));
}

void Scene::Clear()
{
    mRenderItems.clear();
    mLights.clear();
}

void Scene::Update(float dt)
{
    for (auto& ri : mRenderItems)
    {
        if (ri->NumFramesDirty > 0)
        {
            // Обновляем матрицы трансформации
            ri->ScaleM = XMMatrixScaling(ri->Scale.x, ri->Scale.y, ri->Scale.z);
            ri->RotationM = XMMatrixRotationRollPitchYaw(ri->RotationAngle.x, ri->RotationAngle.y, ri->RotationAngle.z);
            ri->TranslationM = XMMatrixTranslation(ri->Position.x, ri->Position.y, ri->Position.z);

            XMStoreFloat4x4(&ri->World, ri->ScaleM * ri->RotationM * ri->TranslationM);

            ri->NumFramesDirty--;
        }
    }
}

void Scene::Draw(ID3D12GraphicsCommandList* cmdList)
{
    for (auto& ri : mRenderItems)
    {
        if (!ri->Geo) continue;

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void Scene::MarkDirty()
{
    for (auto& ri : mRenderItems)
    {
        ri->NumFramesDirty = gNumFrameResources;
    }
}

void Scene::RemoveRenderItemsByName(const std::string& name)
{
    mRenderItems.erase(
        std::remove_if(
            mRenderItems.begin(),
            mRenderItems.end(),
            [&](const std::unique_ptr<RenderItem>& item) {
                return item->Name == name;
            }),
        mRenderItems.end());

}

void Scene::RemoveLightsByIndex(int index)
{
    if (index >= 0 && index < static_cast<int>(mLights.size())) {
        mLights.erase(mLights.begin() + index);
    }

}

int Scene::LightsGetSize()
{
    return mLights.size();
}

int Scene::RenderItemsGetSize()
{
    return mRenderItems.size();
}

auto& Scene::LightsGetBegine()
{
    return mLights.begin();
}

auto& Scene::RenderItemsGetBegine()
{
    return mRenderItems.begin();
}

RenderItem* Scene::GetRenderItemByIndex(int index)
{
    if (index >= 0 && index < static_cast<int>(mRenderItems.size()))
        return mRenderItems[index].get();
    return nullptr;
}

Light* Scene::GetLightByIndex(int index)
{
    if (index >= 0 && index < static_cast<int>(mLights.size()))
        return mLights[index].get(); 
    return nullptr;
}


std::vector<std::unique_ptr<RenderItem>>& Scene::GetRenderItems()
{
    return mRenderItems;
}

std::vector<std::unique_ptr<Light>>& Scene::GetLights()
{
    return mLights;
}





#pragma once

#include "Common/d3dUtil.h"
#include "Common/MathHelper.h"

#include <vector>
#include <memory>

struct BoundingBoxAABB
{
    DirectX::XMFLOAT3 m_center{};
    DirectX::XMFLOAT3 m_halfDimensions{};

    bool Intersects(const DirectX::XMFLOAT4* planeSet) const;
};

struct TerrainNode
{
    float m_xCoordinate = 0.0f;
    float m_zCoordinate = 0.0f;
    float m_patchSize = 0.0f;

    int m_detailLevel = 0;
    int m_maximumDetail = 0;

    BoundingBoxAABB m_bounds{};
    float m_minimumElevation = 0.0f;
    float m_maximumElevation = 0.0f;

    bool m_isLeaf = true;
    std::unique_ptr<TerrainNode> m_childPatches[4];

    bool m_isVisible = false;
    unsigned int m_bufferIndex = 0;

    TerrainNode() = default;
};

class QuadTree
{
public:
    QuadTree();
    ~QuadTree() = default;

    void Initialize(float worldExtent, float minPatchSize, int maxDetailLevels);
    void Update(const DirectX::XMFLOAT3& viewerLocation, const DirectX::XMFLOAT4* frustumBoundaries);
    void GetVisibleNodes(std::vector<TerrainNode*>& outputList);
    void SetHeightRange(float xPos, float zPos, float regionSize, float minY, float maxY);

    int GetVisibleNodeCount() const { return m_visiblePatchCount; }
    int GetTotalNodeCount() const { return m_totalPatchCount; }

    void SetLODDistances(const std::vector<float>& thresholds) { m_lodThresholds = thresholds; }

private:
    void ConstructTree(TerrainNode* patch, float xPos, float zPos, float patchSize, int depth);
    void ProcessNode(TerrainNode* patch, const DirectX::XMFLOAT3& viewerLocation, const DirectX::XMFLOAT4* frustumBoundaries);
    void GatherVisible(TerrainNode* patch, std::vector<TerrainNode*>& outputList);

    int DetermineLOD(const TerrainNode* patch, const DirectX::XMFLOAT3& viewerLocation) const;
    bool ShouldFragment(const TerrainNode* patch, const DirectX::XMFLOAT3& viewerLocation) const;

private:
    std::unique_ptr<TerrainNode> m_rootPatch;

    float m_worldExtent = 0.0f;
    float m_minimumPatchSize = 0.0f;
    int m_maximumDetailLevels = 0;

    std::vector<float> m_lodThresholds;

    int m_visiblePatchCount = 0;
    int m_totalPatchCount = 0;
    unsigned int m_nextBufferIndex = 0;
};
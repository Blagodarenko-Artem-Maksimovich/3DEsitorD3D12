#include "QuadTree.h"
#include <cmath>
#include <algorithm>

using namespace DirectX;

bool BoundingBoxAABB::Intersects(const XMFLOAT4* planeSet) const
{
    for (int idx = 0; idx < 6; ++idx)
    {
        const XMFLOAT4& plane = planeSet[idx];

        XMFLOAT3 vertex;
        vertex.x = (plane.x >= 0.0f) ? (m_center.x + m_halfDimensions.x) : (m_center.x - m_halfDimensions.x);
        vertex.y = (plane.y >= 0.0f) ? (m_center.y + m_halfDimensions.y) : (m_center.y - m_halfDimensions.y);
        vertex.z = (plane.z >= 0.0f) ? (m_center.z + m_halfDimensions.z) : (m_center.z - m_halfDimensions.z);

        float distance = plane.x * vertex.x + plane.y * vertex.y + plane.z * vertex.z + plane.w;
        if (distance < 0.0f)
            return false;
    }
    return true;
}

QuadTree::QuadTree()
{
    m_worldExtent = 0.0f;
    m_minimumPatchSize = 0.0f;
    m_maximumDetailLevels = 0;
    m_visiblePatchCount = 0;
    m_totalPatchCount = 0;
    m_nextBufferIndex = 0;
}

void QuadTree::Initialize(float worldExtent, float minPatchSize, int maxDetailLevels)
{
    m_worldExtent = worldExtent;
    m_minimumPatchSize = minPatchSize;
    m_maximumDetailLevels = maxDetailLevels;

    m_visiblePatchCount = 0;
    m_totalPatchCount = 0;
    m_nextBufferIndex = 0;

    if (m_lodThresholds.empty())
    {
        m_lodThresholds.resize((size_t)maxDetailLevels);

        for (int i = 0; i < maxDetailLevels; ++i)
        {
            int shift = (maxDetailLevels - i - 1);
            float base = m_minimumPatchSize * (float)(1 << shift);
            m_lodThresholds[(size_t)i] = base * 2.0f;
        }
    }

    m_rootPatch = std::make_unique<TerrainNode>();
    ConstructTree(m_rootPatch.get(), 0.0f, 0.0f, m_worldExtent, 0);
}

void QuadTree::ConstructTree(TerrainNode* patch, float xPos, float zPos, float patchSize, int depth)
{
    patch->m_xCoordinate = xPos;
    patch->m_zCoordinate = zPos;
    patch->m_patchSize = patchSize;

    patch->m_detailLevel = depth;
    patch->m_maximumDetail = m_maximumDetailLevels - 1;

    patch->m_minimumElevation = 0.0f;
    patch->m_maximumElevation = 100.0f;

    float midElevation = (patch->m_minimumElevation + patch->m_maximumElevation) * 0.5f;
    float halfVertical = (patch->m_maximumElevation - patch->m_minimumElevation) * 0.5f + 10.0f;

    patch->m_bounds.m_center = XMFLOAT3(xPos, midElevation, zPos);
    patch->m_bounds.m_halfDimensions = XMFLOAT3(patchSize * 0.5f, halfVertical, patchSize * 0.5f);

    ++m_totalPatchCount;

    bool canSubdivide = (patchSize > m_minimumPatchSize) && (depth < (m_maximumDetailLevels - 1));
    if (!canSubdivide)
    {
        patch->m_isLeaf = true;
        return;
    }

    patch->m_isLeaf = false;

    float halfSpan = patchSize * 0.5f;
    float quarterSpan = patchSize * 0.25f;

    for (int i = 0; i < 4; ++i)
        patch->m_childPatches[i] = std::make_unique<TerrainNode>();

    ConstructTree(patch->m_childPatches[0].get(), xPos - quarterSpan, zPos + quarterSpan, halfSpan, depth + 1);
    ConstructTree(patch->m_childPatches[1].get(), xPos + quarterSpan, zPos + quarterSpan, halfSpan, depth + 1);
    ConstructTree(patch->m_childPatches[2].get(), xPos - quarterSpan, zPos - quarterSpan, halfSpan, depth + 1);
    ConstructTree(patch->m_childPatches[3].get(), xPos + quarterSpan, zPos - quarterSpan, halfSpan, depth + 1);
}

void QuadTree::Update(const XMFLOAT3& viewerLocation, const XMFLOAT4* frustumBoundaries)
{
    m_visiblePatchCount = 0;
    m_nextBufferIndex = 0;

    if (m_rootPatch)
        ProcessNode(m_rootPatch.get(), viewerLocation, frustumBoundaries);
}

void QuadTree::ProcessNode(TerrainNode* patch, const XMFLOAT3& viewerLocation, const XMFLOAT4* frustumBoundaries)
{
    patch->m_isVisible = patch->m_bounds.Intersects(frustumBoundaries);
    if (!patch->m_isVisible)
        return;

    patch->m_detailLevel = DetermineLOD(patch, viewerLocation);

    bool traverseChildren = (!patch->m_isLeaf) && ShouldFragment(patch, viewerLocation);

    if (traverseChildren)
    {
        patch->m_isVisible = false;

        for (int i = 0; i < 4; ++i)
        {
            if (patch->m_childPatches[i])
                ProcessNode(patch->m_childPatches[i].get(), viewerLocation, frustumBoundaries);
        }
        return;
    }

    patch->m_bufferIndex = m_nextBufferIndex++;
    ++m_visiblePatchCount;
}

int QuadTree::DetermineLOD(const TerrainNode* patch, const XMFLOAT3& viewerLocation) const
{
    float centerX = patch->m_xCoordinate;
    float centerZ = patch->m_zCoordinate;
    float centerY = (patch->m_minimumElevation + patch->m_maximumElevation) * 0.5f;

    float dx = viewerLocation.x - centerX;
    float dy = viewerLocation.y - centerY;
    float dz = viewerLocation.z - centerZ;

    float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    for (size_t i = 0; i < m_lodThresholds.size(); ++i)
    {
        if (distance < m_lodThresholds[i])
            return (int)i;
    }

    return m_maximumDetailLevels - 1;
}

bool QuadTree::ShouldFragment(const TerrainNode* patch, const XMFLOAT3& viewerLocation) const
{
    if (patch->m_isLeaf)
        return false;

    float dx = viewerLocation.x - patch->m_xCoordinate;
    float dz = viewerLocation.z - patch->m_zCoordinate;

    float horizontalDistance = std::sqrt(dx * dx + dz * dz);

    return horizontalDistance < (patch->m_patchSize * 1.5f);
}

void QuadTree::GetVisibleNodes(std::vector<TerrainNode*>& outputList)
{
    outputList.clear();
    outputList.reserve((size_t)m_visiblePatchCount);

    if (m_rootPatch)
        GatherVisible(m_rootPatch.get(), outputList);
}

void QuadTree::GatherVisible(TerrainNode* patch, std::vector<TerrainNode*>& outputList)
{
    if (patch->m_isVisible)
    {
        outputList.push_back(patch);
        return;
    }

    if (patch->m_isLeaf)
        return;

    for (int i = 0; i < 4; ++i)
    {
        if (patch->m_childPatches[i])
            GatherVisible(patch->m_childPatches[i].get(), outputList);
    }
}

void QuadTree::SetHeightRange(float xPos, float zPos, float regionSize, float minY, float maxY)
{
    (void)xPos; (void)zPos; (void)regionSize;

    if (!m_rootPatch)
        return;

    m_rootPatch->m_minimumElevation = minY;
    m_rootPatch->m_maximumElevation = maxY;

    m_rootPatch->m_bounds.m_center.y = (minY + maxY) * 0.5f;
    m_rootPatch->m_bounds.m_halfDimensions.y = (maxY - minY) * 0.5f + 10.0f;
}
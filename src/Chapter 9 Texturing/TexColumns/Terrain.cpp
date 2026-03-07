#include "Terrain.h"
#include <DirectXCollision.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

void Terrain::SetWorldSize(float sizeXZ)
{
	mWorldSizeXZ = sizeXZ;
}

void Terrain::SetOriginY(float y)
{
	mOriginY = y;
}

void Terrain::SetLODDistances(float maxDistLOD1, float maxDistLOD2)
{
	mMaxDistLOD1 = maxDistLOD1;
	mMaxDistLOD2 = maxDistLOD2;
}

void Terrain::BuildQuadtree()
{
	mRoot = std::make_unique<TerrainNode>();
	mRoot->LOD = 0;
	mRoot->TileX = 0;
	mRoot->TileZ = 0;
	// Full terrain in XZ: [-mWorldSizeXZ/2, mWorldSizeXZ/2], Y from mOriginY to mOriginY + mHeightScale
	XMFLOAT3 center(0.f, mOriginY + mHeightScale * 0.5f, 0.f);
	XMFLOAT3 extents(mWorldSizeXZ * 0.5f, mHeightScale * 0.5f, mWorldSizeXZ * 0.5f);
	mRoot->Bounds = BoundingBox(center, extents);
	BuildNode(*mRoot, 0, 0, 0, 1);
}

void Terrain::BuildNode(TerrainNode& node, int lod, int tileX, int tileZ, int tilesPerSide)
{
	node.LOD = lod;
	node.TileX = tileX;
	node.TileZ = tileZ;
	if (lod >= 2) return; // leaf at LOD2
	int childTiles = (lod == 0) ? 2 : 4; // LOD0 -> 2x2 children, LOD1 -> 4x4 children
	float half = node.Bounds.Extents.x;
	float quarter = half * 0.5f;
	XMVECTOR c = XMLoadFloat3(&node.Bounds.Center);
	for (int i = 0; i < 4; ++i)
	{
		node.Children[i] = std::make_unique<TerrainNode>();
		int cx = i % 2;
		int cz = i / 2;
		float ox = (cx == 0) ? -quarter : quarter;
		float oz = (cz == 0) ? -quarter : quarter;
		XMFLOAT3 childCenter;
		XMStoreFloat3(&childCenter, c + XMVectorSet(ox, 0.f, oz, 0.f));
		node.Children[i]->Bounds = BoundingBox(childCenter, XMFLOAT3(quarter, node.Bounds.Extents.y, quarter));
		int nt = (lod == 0) ? 2 : 4;
		int childTileX = tileX * 2 + cx;
		int childTileZ = tileZ * 2 + cz;
		BuildNode(*node.Children[i], lod + 1, childTileX, childTileZ, nt);
	}
}

bool Terrain::IntersectsFrustum(const BoundingBox& box, const XMFLOAT4X4& viewProj) const
{
	// PassConstants stores ViewProj as transpose (column-major in memory: col i = m[i], m[i+4], m[i+8], m[i+12])
	// So viewProj ROW r = (m[r], m[r+4], m[r+8], m[r+12]). Hartmann-Gribb needs rows.
	const float* m = &viewProj.m[0][0];
	auto getRow = [m](int r) -> XMVECTOR {
		return XMVectorSet(m[r], m[r+4], m[r+8], m[r+12]);
	};
	XMVECTOR R0 = getRow(0), R1 = getRow(1), R2 = getRow(2), R3 = getRow(3);
	XMVECTOR planes[6] = {
		XMVectorAdd(R3, R0),   // Left
		XMVectorSubtract(R3, R0), // Right
		XMVectorAdd(R3, R1),   // Bottom
		XMVectorSubtract(R3, R1), // Top
		R2,                    // Near
		XMVectorSubtract(R3, R2)  // Far
	};
	for (int i = 0; i < 6; ++i)
	{
		XMVECTOR p = planes[i];
		float len = XMVectorGetX(XMVector3Length(p));
		if (len < 1e-6f) continue;
		p = XMVectorScale(p, 1.f / len);
		float d = -XMVectorGetW(planes[i]) / len;
		// AABB: positive vertex along plane normal
		float nx = XMVectorGetX(p), ny = XMVectorGetY(p), nz = XMVectorGetZ(p);
		XMFLOAT3 pVertex(
			box.Center.x + (nx >= 0.f ? box.Extents.x : -box.Extents.x),
			box.Center.y + (ny >= 0.f ? box.Extents.y : -box.Extents.y),
			box.Center.z + (nz >= 0.f ? box.Extents.z : -box.Extents.z)
		);
		if (nx * pVertex.x + ny * pVertex.y + nz * pVertex.z + d < 0.f)
			return false;
	}
	return true;
}

float Terrain::DistanceToNode(const BoundingBox& box, const XMFLOAT3& eyePos) const
{
	XMVECTOR e = XMLoadFloat3(&eyePos);
	XMVECTOR c = XMLoadFloat3(&box.Center);
	XMVECTOR d = XMVector3LengthEst(XMVectorSubtract(e, c));
	return XMVectorGetX(d);
}

void Terrain::SelectLOD(const TerrainNode& node, const XMFLOAT4X4& viewProj,
	const XMFLOAT3& eyePos, float maxDistLOD1, float maxDistLOD2)
{
	if (!IntersectsFrustum(node.Bounds, viewProj))
		return;
	float dist = DistanceToNode(node.Bounds, eyePos);
	if (node.IsLeaf() || node.LOD == 2)
	{
		TerrainTile tile;
		FillTileFromNode(node, tile);
		tile.HeightmapSrvIndex = node.HeightmapSrvIndex;
		mVisibleTiles.push_back(tile);
		return;
	}
	bool useChild = (node.LOD == 0 && dist < maxDistLOD1) || (node.LOD == 1 && dist < maxDistLOD2);
	if (useChild && node.Children[0])
	{
		for (int i = 0; i < 4; ++i)
			SelectLOD(*node.Children[i], viewProj, eyePos, maxDistLOD1, maxDistLOD2);
	}
	else
	{
		TerrainTile tile;
		FillTileFromNode(node, tile);
		tile.HeightmapSrvIndex = node.HeightmapSrvIndex;
		mVisibleTiles.push_back(tile);
	}
}

void Terrain::Update(const XMFLOAT4X4& viewProj, const XMFLOAT3& eyePos)
{
	mVisibleTiles.clear();
	if (!mRoot) return;
	SelectLOD(*mRoot, viewProj, eyePos, mMaxDistLOD1, mMaxDistLOD2);
}

void Terrain::FillTileFromNode(const TerrainNode& node, TerrainTile& outTile) const
{
	outTile.LOD = node.LOD;
	outTile.TileX = node.TileX;
	outTile.TileZ = node.TileZ;
	outTile.HeightmapSrvIndex = node.HeightmapSrvIndex;
	outTile.AABB = node.Bounds;
	float half = node.Bounds.Extents.x;
	XMMATRIX world = XMMatrixScaling(half * 2.f, mHeightScale, half * 2.f);
	world = XMMatrixMultiply(world, XMMatrixTranslationFromVector(XMLoadFloat3(&node.Bounds.Center)));
	XMStoreFloat4x4(&outTile.World, XMMatrixTranspose(world));
	outTile.PrevWorld = outTile.World;
}

void Terrain::AssignHeightmapIndicesRecursive(TerrainNode& node,
	const std::vector<int>& lod0, const std::vector<int>& lod1, const std::vector<int>& lod2)
{
	if (node.LOD == 0 && !lod0.empty()) node.HeightmapSrvIndex = lod0[0];
	else if (node.LOD == 1 && node.TileX >= 0 && node.TileX < 2 && node.TileZ >= 0 && node.TileZ < 2)
	{
		int idx = node.TileZ * 2 + node.TileX;
		if (idx < (int)lod1.size()) node.HeightmapSrvIndex = lod1[idx];
	}
	else if (node.LOD == 2 && node.TileX >= 0 && node.TileX < 4 && node.TileZ >= 0 && node.TileZ < 4)
	{
		int idx = node.TileZ * 4 + node.TileX;
		if (idx < (int)lod2.size()) node.HeightmapSrvIndex = lod2[idx];
	}
	for (int i = 0; i < 4; ++i)
		if (node.Children[i])
			AssignHeightmapIndicesRecursive(*node.Children[i], lod0, lod1, lod2);
}

void Terrain::AssignHeightmapIndices(const std::vector<int>& lod0Indices,
	const std::vector<int>& lod1Indices,
	const std::vector<int>& lod2Indices)
{
	if (mRoot)
		AssignHeightmapIndicesRecursive(*mRoot, lod0Indices, lod1Indices, lod2Indices);
}

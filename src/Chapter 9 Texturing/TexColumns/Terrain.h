#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include <DirectXCollision.h>
#include <memory>
#include <vector>

// LOD levels: 0 = 001 (512), 1 = 002 (1024), 2 = 003 (2048)
constexpr int kTerrainLODLevels = 3;
// Tile counts per level: 1, 2x2, 4x4
constexpr int kTerrainTilesLOD0 = 1;
constexpr int kTerrainTilesLOD1 = 4;
constexpr int kTerrainTilesLOD2 = 16;

struct TerrainTile
{
	int LOD = 0;           // 0, 1, 2
	int TileX = 0;         // tile index in X (0..1 for LOD1, 0..3 for LOD2)
	int TileZ = 0;         // tile index in Z
	int HeightmapSrvIndex = -1; // index into descriptor heap for this tile's heightmap
	DirectX::BoundingBox AABB;   // world AABB for frustum culling
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 PrevWorld = MathHelper::Identity4x4();
};

// Quadtree node for LOD selection
struct TerrainNode
{
	DirectX::BoundingBox Bounds;
	int LOD = 0;
	int TileX = 0;
	int TileZ = 0;
	int HeightmapSrvIndex = -1;
	std::unique_ptr<TerrainNode> Children[4];
	bool IsLeaf() const { return !Children[0]; }
};

class Terrain
{
public:
	Terrain() = default;

	// World size in XZ (e.g. 100 = terrain from -50..50)
	void SetWorldSize(float sizeXZ);
	// Origin Y: terrain AABB min Y = originY, center Y = originY + heightScale*0.5
	void SetOriginY(float y);
	float GetOriginY() const { return mOriginY; }
	// Height scale: heightmap value 0..1 multiplied by this
	void SetHeightScale(float scale) { mHeightScale = scale; }
	float GetHeightScale() const { return mHeightScale; }
	// LOD distance thresholds (distance from camera: below LOD1 use 4 tiles, below LOD2 use 16)
	void SetLODDistances(float maxDistLOD1, float maxDistLOD2);
	float GetMaxDistLOD1() const { return mMaxDistLOD1; }
	float GetMaxDistLOD2() const { return mMaxDistLOD2; }

	// Build quadtree: LOD0 = 1 tile, LOD1 = 4 children, LOD2 = 16 leaves
	void BuildQuadtree();

	// Update visible tiles: frustum culling + LOD by distance, fill mVisibleTiles
	void Update(const DirectX::XMFLOAT4X4& viewProj, const DirectX::XMFLOAT3& eyePos);

	const std::vector<TerrainTile>& GetVisibleTiles() const { return mVisibleTiles; }

	// Tile world transform and AABB for a node
	void FillTileFromNode(const TerrainNode& node, TerrainTile& outTile) const;
	// Assign heightmap SRV index to each node (call after textures loaded)
	void AssignHeightmapIndices(const std::vector<int>& lod0Indices,
		const std::vector<int>& lod1Indices,
		const std::vector<int>& lod2Indices);

	const TerrainNode* GetRoot() const { return mRoot.get(); }
	float GetWorldSizeXZ() const { return mWorldSizeXZ; }

private:
	float mWorldSizeXZ = 100.0f;
	float mHeightScale = 50.0f;
	float mOriginY = 0.0f;
	float mMaxDistLOD1 = 60.0f;
	float mMaxDistLOD2 = 30.0f;
	std::unique_ptr<TerrainNode> mRoot;
	std::vector<TerrainTile> mVisibleTiles;

	void BuildNode(TerrainNode& node, int lod, int tileX, int tileZ, int tilesPerSide);
	bool IntersectsFrustum(const DirectX::BoundingBox& box, const DirectX::XMFLOAT4X4& viewProj) const;
	float DistanceToNode(const DirectX::BoundingBox& box, const DirectX::XMFLOAT3& eyePos) const;
	void SelectLOD(const TerrainNode& node, const DirectX::XMFLOAT4X4& viewProj,
		const DirectX::XMFLOAT3& eyePos, float maxDistLOD1, float maxDistLOD2);
	void AssignHeightmapIndicesRecursive(TerrainNode& node,
		const std::vector<int>& lod0, const std::vector<int>& lod1, const std::vector<int>& lod2);
};

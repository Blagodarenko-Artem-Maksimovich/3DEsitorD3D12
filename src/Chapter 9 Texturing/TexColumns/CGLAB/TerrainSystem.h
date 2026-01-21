#pragma once
#include "RenderItem.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;





struct AABB
{
	XMFLOAT3 minPoint;
	XMFLOAT3 maxPoint;
	BoundingBox aabb;
	bool IntersectsFrustum(const XMFLOAT4 frustumPlanes[6]) const;
};
struct TerrainTile
{
	XMFLOAT3 worldPos;          // Позиция тайла в мире
	int lodLevel;               // Уровень детализации (0 - самый детальный)
	float tileSize;             // Размер тайла в мировых единицах
	BoundingBox boundingBox;           // Ограничивающий параллелепипед
	bool isVisible = true;             // Видимость после culling
	int tileIndex;
	int renderItemIndex; // Render item для тайла
	int NumFramesDirty;
};


struct QuadTreeNode
{
	TerrainTile* tile;          
	std::unique_ptr<QuadTreeNode> children[4]; // Дочерние узлы
	BoundingBox boundingBox;           // AABB всего поддерева
	int depth;                  // Глубина в дереве

	// Методы
	bool ShouldSplit(const XMFLOAT3& cameraPos, float heightscale, int mapsize) const;
	void UpdateVisibility(BoundingFrustum& frustum, const XMFLOAT3& cameraPos, std::vector<TerrainTile*>& visibleTiles,float heightscale, int mapsize);
};

class TerrainSystem
{
public:
	TerrainSystem() {};

	void Initialize(ID3D12Device* device, int HeightMapIndex,
		float worldSize, int maxLOD);
	void Update(const XMFLOAT3& cameraPos, BoundingFrustum& frustum);
	std::vector<std::shared_ptr<TerrainTile>>& GetAllTiles();
	void GetVisibleTiles(std::vector<TerrainTile*>& outTiles);
	float m_worldSize;
	int m_hmapIndex;
	float m_heightScale;
	int renderlodlevel = 0;
	int tileRenderIndex = 0;
	bool colordebug = false;
	bool showborders = false;
	bool wireframe = false;
	bool dynamicLOD = true;
	bool renderOneTile = false;
private:
	std::unique_ptr<QuadTreeNode> m_rootNode;
	ComPtr<ID3D12Resource> m_heightmapTexture;
	std::vector<std::shared_ptr<TerrainTile>>m_allTiles;
	std::vector<TerrainTile*> m_visibleTiles;
	int m_maxLOD;
	int tileIndex = 0;
	void BuildQuadTree(QuadTreeNode* node, int x, int y, int size, int depth);
	BoundingBox CalculateTileAABB(const XMFLOAT3& pos, float size, float minHeight, float maxHeight);
};

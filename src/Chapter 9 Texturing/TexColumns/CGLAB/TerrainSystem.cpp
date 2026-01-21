#include "TerrainSystem.h"
#include "CGLAB.h"
// Реализация методов terrain системы


bool QuadTreeNode::ShouldSplit(const XMFLOAT3& cameraPos, float heightscale,int mapsize) const
{
    auto camPos = cameraPos;
    camPos.y = 0;
    XMVECTOR camPosVec = XMLoadFloat3(&camPos);
    float lodneeddist = ( mapsize/2.0f - depth * mapsize/16);
    BoundingSphere sphere;
    sphere.Center = cameraPos;
    sphere.Radius = lodneeddist;
    if (sphere.Intersects(boundingBox))
    {
        return true;
    }
    return false;
}

// 4. Обновление видимости в квадродереве
void QuadTreeNode::UpdateVisibility(BoundingFrustum& frustum, const XMFLOAT3& cameraPos, std::vector<TerrainTile*>& visibleTiles,float heightscale, int mapsize)
{
    if (frustum.Contains(boundingBox) == DISJOINT)
    {
        return; // Узел полностью не виден
    }

    // Если узел является "листом" (нет дочерних узлов) или не нужно его разбивать
    if (!children[0] || !ShouldSplit(cameraPos, heightscale, mapsize))
    {
        // Рендерим текущий узел (тайл)
        if (tile)
        {
            visibleTiles.push_back(tile);
        }
    }
    else // Если нужно разбивать
    {
        // Рекурсивно обновляем видимость дочерних узлов
        for (int i = 0; i < 4; i++)
        {
            if (children[i])
            {
                children[i]->UpdateVisibility(frustum, cameraPos, visibleTiles,heightscale, mapsize);
            }
        }
    }
}

// 5. Инициализация terrain системы
void TerrainSystem::Initialize(ID3D12Device* device, int HeightMapIndex,
    float worldSize, int maxLOD)
{
    m_worldSize = worldSize;
    m_maxLOD = maxLOD;
    m_heightScale = 50.0f; // настраиваемый параметр
    m_hmapIndex = HeightMapIndex;
    // Загрузка heightmap (упрощенная версия - нужно адаптировать под ваш код)
    // ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device, cmdList, 
    //     heightmapPath.c_str(), m_heightmapTexture, uploadHeap));

    // Создаем корневой узел квадродерева
    m_rootNode = std::make_unique<QuadTreeNode>();
    m_rootNode->depth = 0;

    // Строим квадродерево рекурсивно
    int initialSize = (int)worldSize; // 2^maxLOD
    BuildQuadTree(m_rootNode.get(), 0, 0, initialSize, 0);
}

void TerrainSystem::BuildQuadTree(QuadTreeNode* node, int x, int y, int size, int depth)
{
    node->depth = depth;
    // Вычисляем мировые координаты узла
    float tileSize = m_worldSize / (1 << depth);

    // Вычисляем AABB для этого узла.
    // Пока что упрощенно, без учета хайтмапы
    node->boundingBox = CalculateTileAABB(XMFLOAT3((float)x, 0, (float)y), tileSize, -10.0f, 400.0f);
   
    auto tile = std::make_unique<TerrainTile>();
    tile->worldPos = XMFLOAT3((float)x, 0, (float)y);
    tile->lodLevel = depth;
    tile->tileSize = tileSize;
    tile->isVisible = true;
    tile->boundingBox = node->boundingBox;
    tile->tileIndex = tileIndex++;
    m_allTiles.push_back(std::move(tile));
    node->tile = m_allTiles.back().get(); // Указываем на созданный тайл
    // Если мы достигли максимальной глубины, создаем тайл.
    if (depth != m_maxLOD)
    {
        int halfSize = size / 2;
        for (int i = 0; i < 4; i++)
        {
            node->children[i] = std::make_unique<QuadTreeNode>();
            int childX = x + (i % 2) * halfSize;
            int childY = y + (i / 2) * halfSize;
            BuildQuadTree(node->children[i].get(), childX, childY, halfSize, depth + 1);
        }
    }
}
BoundingBox TerrainSystem::CalculateTileAABB(const XMFLOAT3& pos, float size, float minHeight, float maxHeight)
{
    BoundingBox aabb;
    auto minPoint = XMFLOAT3(pos.x, 0, pos.z);
    auto maxPoint = XMFLOAT3(pos.x + size, 100, pos.z + size); // 200 = maxheightscale
    XMVECTOR pt1 = XMLoadFloat3(&minPoint);
    XMVECTOR pt2 = XMLoadFloat3(&maxPoint);
    BoundingBox::CreateFromPoints(aabb, pt1, pt2);
    return aabb;
}

void TerrainSystem::Update(const XMFLOAT3& cameraPos, BoundingFrustum& frustum)
{
    m_visibleTiles.clear();
    if (m_rootNode)
    {
        m_rootNode->UpdateVisibility(frustum, cameraPos, m_visibleTiles, m_heightScale, (int)m_worldSize);
    }
}

std::vector<std::shared_ptr<TerrainTile>>& TerrainSystem::GetAllTiles()
{
    return m_allTiles;
}

void TerrainSystem::GetVisibleTiles(std::vector<TerrainTile*>& outTiles)
{
    outTiles = m_visibleTiles;
}
void CGLAB::GenerateTileGeometry(const XMFLOAT3& worldPos, float tileSize, int lodLevel,
    std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices)
{
    int baseResolution = 16;
    float Factor = 1;
    int resolution = static_cast<int>(baseResolution * std::pow(Factor, lodLevel));

    vertices.clear();
    indices.clear();

    float stepSize = tileSize / (resolution - 1);
    float skirtDepth = 10; // Уменьшил глубину юбки

    // 1. Генерируем основные вершины тайла (как раньше)
    for (int z = 0; z < resolution; z++)
    {
        for (int x = 0; x < resolution; x++)
        {
            Vertex vertex;
            vertex.Pos = XMFLOAT3(worldPos.x + x * stepSize, 0.0f, worldPos.z + z * stepSize);
            vertex.TexC = XMFLOAT2((float)x / (resolution - 1), (float)z / (resolution - 1));
            vertex.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
            vertex.Tangent = XMFLOAT3(1.0f, 0.0f, 0.0f);
            vertices.push_back(vertex);
        }
    }

    int mainVertexCount = static_cast<int>(vertices.size());

    // 2. Создаем вершины юбки (дублируем периметр и смещаем вниз)
    // Левая сторона (x = 0)
    for (int z = 0; z < resolution; z++)
    {
        Vertex vertex = vertices[z * resolution + 0]; // копируем существующую вершину
        vertex.Pos.y = -skirtDepth; // смещаем вниз
        vertices.push_back(vertex);
    }

    // Правая сторона (x = resolution-1)  
    for (int z = 0; z < resolution; z++)
    {
        Vertex vertex = vertices[z * resolution + (resolution - 1)];
        vertex.Pos.y = -skirtDepth;
        vertices.push_back(vertex);
    }

    // Нижняя сторона (z = 0), исключаем углы
    for (int x = 1; x < resolution - 1; x++)
    {
        Vertex vertex = vertices[0 * resolution + x];
        vertex.Pos.y = -skirtDepth;
        vertices.push_back(vertex);
    }

    // Верхняя сторона (z = resolution-1), исключаем углы
    for (int x = 1; x < resolution - 1; x++)
    {
        Vertex vertex = vertices[(resolution - 1) * resolution + x];
        vertex.Pos.y = -skirtDepth;
        vertices.push_back(vertex);
    }

    // 3. Генерируем индексы для основного тайла
    for (int z = 0; z < resolution - 1; z++)
    {
        for (int x = 0; x < resolution - 1; x++)
        {
            UINT topLeft = z * resolution + x;
            UINT topRight = topLeft + 1;
            UINT bottomLeft = (z + 1) * resolution + x;
            UINT bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    // 4. Индексы для юбки - простая версия
    int leftSkirtStart = mainVertexCount;
    int rightSkirtStart = leftSkirtStart + resolution;
    int bottomSkirtStart = rightSkirtStart + resolution;
    int topSkirtStart = bottomSkirtStart + (resolution - 2);

    // Левая юбка
    for (int z = 0; z < resolution - 1; z++)
    {
        UINT edge1 = z * resolution;
        UINT edge2 = (z + 1) * resolution;
        UINT skirt1 = leftSkirtStart + z;
        UINT skirt2 = leftSkirtStart + z + 1;

        indices.push_back(edge1);
        indices.push_back(skirt1);
        indices.push_back(edge2);

        indices.push_back(edge2);
        indices.push_back(skirt1);
        indices.push_back(skirt2);
    }

    // Правая юбка  
    for (int z = 0; z < resolution - 1; z++)
    {
        UINT edge1 = z * resolution + (resolution - 1);
        UINT edge2 = (z + 1) * resolution + (resolution - 1);
        UINT skirt1 = rightSkirtStart + z;
        UINT skirt2 = rightSkirtStart + z + 1;

        indices.push_back(edge1);
        indices.push_back(edge2);
        indices.push_back(skirt1);

        indices.push_back(edge2);
        indices.push_back(skirt2);
        indices.push_back(skirt1);
    }

    // Нижняя юбка
    for (int x = 1; x < resolution - 2; x++)
    {
        UINT edge1 = x;
        UINT edge2 = x + 1;
        UINT skirt1 = bottomSkirtStart + (x - 1);
        UINT skirt2 = bottomSkirtStart + x;

        indices.push_back(edge1);
        indices.push_back(edge2);
        indices.push_back(skirt1);

        indices.push_back(edge2);
        indices.push_back(skirt2);
        indices.push_back(skirt1);
    }

    // Верхняя юбка
    for (int x = 1; x < resolution - 2; x++)
    {
        UINT edge1 = (resolution - 1) * resolution + x;
        UINT edge2 = (resolution - 1) * resolution + x + 1;
        UINT skirt1 = topSkirtStart + (x - 1);
        UINT skirt2 = topSkirtStart + x;

        indices.push_back(edge1);
        indices.push_back(skirt1);
        indices.push_back(edge2);

        indices.push_back(edge2);
        indices.push_back(skirt1);
        indices.push_back(skirt2);
    }
}

void CGLAB::BuildTerrainGeometry()
{
    auto terrainGeo = std::make_unique<MeshGeometry>();
    terrainGeo->Name = "terrainGeo";

    // Получаем тайлы из terrain системы
    auto& allTiles = m_terrainSystem->GetAllTiles();

    // Создаем один большой массив вершин для всех тайлов
    std::vector<Vertex> allVertices;
    std::vector<std::uint32_t> allIndices;

    for (int tileIdx = 0; tileIdx < allTiles.size(); tileIdx++)
    {
        auto& tile = allTiles[tileIdx];

        std::vector<Vertex> tileVertices;
        std::vector<std::uint32_t> tileIndices;

        GenerateTileGeometry(tile->worldPos, tile->tileSize, tile->lodLevel, tileVertices, tileIndices);

        // Смещаем индексы на количество уже добавленных вершин
        UINT baseVertex = static_cast<int>(allVertices.size());
        for (auto& index : tileIndices)
        {
            index += baseVertex;
        }

        // Сохраняем submesh
        SubmeshGeometry submesh;
        submesh.IndexCount = (UINT)tileIndices.size();
        submesh.StartIndexLocation = (UINT)allIndices.size();
        submesh.BaseVertexLocation = 0;

        std::string submeshName = "tile_" + std::to_string(tileIdx) + "_LOD_" + std::to_string(tile->lodLevel);
        terrainGeo->DrawArgs[submeshName] = submesh;

        // Добавляем в общие массивы
        allVertices.insert(allVertices.end(), tileVertices.begin(), tileVertices.end());
        allIndices.insert(allIndices.end(), tileIndices.begin(), tileIndices.end());
        
    }

    // 3. Создание GPU-буферов (остаётся как было)
    const UINT vbByteSize = (UINT)allVertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)allIndices.size() * sizeof(std::uint32_t);

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &terrainGeo->VertexBufferCPU));
    CopyMemory(terrainGeo->VertexBufferCPU->GetBufferPointer(), allVertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &terrainGeo->IndexBufferCPU));
    CopyMemory(terrainGeo->IndexBufferCPU->GetBufferPointer(), allIndices.data(), ibByteSize);

    terrainGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        allVertices.data(), vbByteSize,
        terrainGeo->VertexBufferUploader);

    terrainGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        allIndices.data(), ibByteSize,
        terrainGeo->IndexBufferUploader);

    terrainGeo->VertexByteStride = sizeof(Vertex);
    terrainGeo->VertexBufferByteSize = vbByteSize;
    terrainGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
    terrainGeo->IndexBufferByteSize = ibByteSize;

    mGeometries[terrainGeo->Name] = std::move(terrainGeo);
}



void CGLAB::UpdateTerrain(const GameTimer& gt)
{
    if (!m_terrainSystem)
        return;

    // Обновляем позицию камеры
    XMVECTOR camPos = cam.GetPosition();
    XMFLOAT3 cameraPosition;
    XMStoreFloat3(&cameraPosition, camPos);

    // Извлекаем плоскости frustum
    XMMATRIX view = XMLoadFloat4x4(&mView);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    // Обновляем terrain систему. Это заполняет m_visibleTiles
    m_terrainSystem->Update(cameraPosition, cam.GetFrustum());
    m_terrainSystem->m_heightScale = heightScale;

}


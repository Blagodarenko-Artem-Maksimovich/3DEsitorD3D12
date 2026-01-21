#include "GeometryManager.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
void GeometryManager::InitAll()
{
}

void GeometryManager::BuildBasicGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdlist)
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(-1.0f, 1.0f, .5f, .5f, 0.0f);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		quad.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

	for (int i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Normal = quad.Vertices[i].Normal;
		vertices[k].TexC = quad.Vertices[i].TexC;
		vertices[k].TangentU = quad.Vertices[i].TangentU;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
		cmdlist, vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
		cmdlist, indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["quad"] = quadSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void GeometryManager::BuildGeometryFromFile(ID3D12Device* device, ID3D12GraphicsCommandList* cmdlist, const std::string& filename, const std::string& geometryName)
{
	Assimp::Importer importer;

	// Флаги для постобработки:
	// aiProcess_Triangulate: Превращает все полигоны в треугольники.
	// aiProcess_FlipUVs: Переворачивает V координату текстуры (для DirectX).
	// aiProcess_CalcTangentSpace: Считает тангенты (нужно для Normal Mapping).
	// aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder: Конвертация в систему координат DirectX.
	const UINT pFlags =
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_FlipUVs |
		aiProcess_CalcTangentSpace |
		aiProcess_MakeLeftHanded |
		aiProcess_FlipWindingOrder;

	const aiScene* scene = importer.ReadFile(filename, pFlags);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::string errorMsg = "Assimp error: " + std::string(importer.GetErrorString());
		MessageBoxA(0, errorMsg.c_str(), "Error", MB_OK);
		return;
	}

	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices; // Используем 32-битные индексы для безопасности

	UINT totalVertexCount = 0;
	UINT totalIndexCount = 0;

	// Создаем геометрию
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = geometryName;

	// Проходим по всем мешам в сцене (файл может содержать несколько частей)
	for (UINT i = 0; i < scene->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[i];
		std::string submeshName = mesh->mName.C_Str();

		// Если имя пустое, даем генеративное
		if (submeshName.empty()) submeshName = geometryName + "_sub_" + std::to_string(i);

		SubmeshGeometry submesh;
		submesh.IndexCount = mesh->mNumFaces * 3;
		submesh.StartIndexLocation = totalIndexCount;
		submesh.BaseVertexLocation = totalVertexCount;

		// --- Вычисление Bounding Box для submesh (опционально, но полезно) ---
		XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
		XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
		XMVECTOR vMin = XMLoadFloat3(&vMinf3);
		XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

		// 1. Обработка Вершин
		for (UINT v = 0; v < mesh->mNumVertices; v++)
		{
			Vertex vertex;

			// Позиция
			vertex.Pos.x = mesh->mVertices[v].x;
			vertex.Pos.y = mesh->mVertices[v].y;
			vertex.Pos.z = mesh->mVertices[v].z;

			// Нормаль
			if (mesh->HasNormals())
			{
				vertex.Normal.x = mesh->mNormals[v].x;
				vertex.Normal.y = mesh->mNormals[v].y;
				vertex.Normal.z = mesh->mNormals[v].z;
			}
			else
			{
				vertex.Normal = { 0.0f, 1.0f, 0.0f };
			}

			// Текстурные координаты (UV)
			if (mesh->mTextureCoords[0]) // Если есть 0-й канал UV
			{
				vertex.TexC.x = mesh->mTextureCoords[0][v].x;
				vertex.TexC.y = mesh->mTextureCoords[0][v].y;
			}
			else
			{
				vertex.TexC = { 0.0f, 0.0f };
			}

			// Тангенты
			if (mesh->HasTangentsAndBitangents())
			{
				vertex.TangentU.x = mesh->mTangents[v].x;
				vertex.TangentU.y = mesh->mTangents[v].y;
				vertex.TangentU.z = mesh->mTangents[v].z;
			}
			else
			{
				vertex.TangentU = { 1.0f, 0.0f, 0.0f };
			}

			// Для BoundingBox
			XMVECTOR P = XMLoadFloat3(&vertex.Pos);
			vMin = XMVectorMin(vMin, P);
			vMax = XMVectorMax(vMax, P);

			vertices.push_back(vertex);
		}

		// Сохраняем границы
		XMStoreFloat3(&submesh.Bounds.Center, 0.5f * (vMin + vMax));
		XMStoreFloat3(&submesh.Bounds.Extents, 0.5f * (vMax - vMin));

		// 2. Обработка Индексов
		for (UINT f = 0; f < mesh->mNumFaces; f++)
		{
			aiFace face = mesh->mFaces[f];
			for (UINT ind = 0; ind < face.mNumIndices; ind++)
			{
				indices.push_back(face.mIndices[ind]);
			}
		}

		// Сохраняем Submesh в DrawArgs
		geo->DrawArgs[submeshName] = submesh;

		totalVertexCount += mesh->mNumVertices;
		totalIndexCount += mesh->mNumFaces * 3;
	}

	// --- Создание буферов (стандартный код DirectX 12) ---

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
		cmdlist, vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
		cmdlist, indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	// ВАЖНО: Используем R32_UINT, так как Assimp может вернуть много вершин
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	mGeometries[geo->Name] = std::move(geo);
}

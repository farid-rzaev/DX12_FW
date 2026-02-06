#include "AssimpLoader.h"

#include <Framework/3RD_Party/Helpers.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <DirectXMath.h>
#include <filesystem>
#include <Windows.h>

using namespace DirectX;

namespace
{
    std::string ToNarrow(const std::wstring& wstr)
    {
        if (wstr.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(size) - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring ToWide(const std::string& str)
    {
        if (str.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring result(static_cast<size_t>(size) - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), size);
        return result;
    }
}

std::vector<LoadedMeshPart> AssimpLoader::Load(
    CommandList& commandList,
    const std::wstring& modelPath,
    const Texture& defaultTexture)
{
    std::vector<LoadedMeshPart> parts;

    std::filesystem::path path(modelPath);
    if (!std::filesystem::exists(path))
    {
        ThrowIfFailed(false, "AssimpLoader: Model file not found.");
        return parts;
    }

    std::string narrowPath = ToNarrow(path.lexically_normal().wstring());
    std::filesystem::path modelDir = path.parent_path();

    Assimp::Importer importer;

    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_FlipUVs |
        aiProcess_MakeLeftHanded |
        aiProcess_FlipWindingOrder;

    const aiScene* scene = importer.ReadFile(narrowPath, flags);
    if (!scene || !scene->HasMeshes())
    {
        ThrowIfFailed(false, importer.GetErrorString());
        return parts;
    }

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const aiMesh* aiMesh = scene->mMeshes[i];
        if (!aiMesh->HasFaces())
            continue;

        VertexCollection vertices;
        IndexCollection indices;

        // Vertices
        for (unsigned int v = 0; v < aiMesh->mNumVertices; ++v)
        {
            XMFLOAT3 pos(
                aiMesh->mVertices[v].x,
                aiMesh->mVertices[v].y,
                aiMesh->mVertices[v].z);

            XMFLOAT3 normal(0, 1, 0);
            if (aiMesh->HasNormals())
            {
                normal.x = aiMesh->mNormals[v].x;
                normal.y = aiMesh->mNormals[v].y;
                normal.z = aiMesh->mNormals[v].z;
            }

            XMFLOAT2 uv(0, 0);
            if (aiMesh->mTextureCoords[0])
            {
                uv.x = aiMesh->mTextureCoords[0][v].x;
                uv.y = aiMesh->mTextureCoords[0][v].y;
            }

            vertices.push_back(VertexPositionNormalTexture(pos, normal, uv));
        }

        // Indices
        for (unsigned int f = 0; f < aiMesh->mNumFaces; ++f)
        {
            const aiFace& face = aiMesh->mFaces[f];
            if (face.mNumIndices != 3)
                continue;
            indices.push_back(static_cast<uint16_t>(face.mIndices[0]));
            indices.push_back(static_cast<uint16_t>(face.mIndices[1]));
            indices.push_back(static_cast<uint16_t>(face.mIndices[2]));
        }

        if (vertices.empty() || indices.empty())
            continue;

        std::unique_ptr<Mesh> mesh = Mesh::CreateFromData(commandList, vertices, indices, true);
        if (!mesh)
            continue;

        LoadedMeshPart part;
        part.mesh = std::move(mesh);
        part.material = Material::White;

        // Diffuse texture
        bool hasDiffuse = false;
        if (aiMesh->mMaterialIndex < scene->mNumMaterials)
        {
            aiMaterial* mat = scene->mMaterials[aiMesh->mMaterialIndex];
            aiString texPath;
            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
            {
                std::string pathStr(texPath.C_Str());
                // Skip embedded textures (path starts with *)
                if (!pathStr.empty() && pathStr[0] != '*')
                {
                    std::filesystem::path fullPath = modelDir / pathStr;
                    std::wstring fullPathW = fullPath.lexically_normal().wstring();
                    if (std::filesystem::exists(fullPath))
                    {
                        try
                        {
                            commandList.LoadTextureFromFile(part.diffuseTexture, fullPathW);
                            hasDiffuse = true;
                        }
                        catch (...) { /* use default */ }
                    }
                }
            }
        }

        if (!hasDiffuse)
            part.diffuseTexture = defaultTexture;

        parts.push_back(std::move(part));
    }

    return parts;
}
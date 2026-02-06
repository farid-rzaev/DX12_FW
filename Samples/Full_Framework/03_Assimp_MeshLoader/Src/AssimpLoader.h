#pragma once

#include <Framework/Material/Mesh.h>
#include <Framework/Material/Texture.h>
#include <Framework/CommandList.h>
#include "Material.h"

#include <memory>
#include <vector>
#include <string>

struct LoadedMeshPart
{
    std::unique_ptr<Mesh> mesh;
    Texture diffuseTexture;   // Loaded or copied from default
    Material material;
};

class AssimpLoader
{
public:
    // Loads an FBX/model file and returns mesh parts.
    // commandList: used for CopyVertexBuffer/CopyIndexBuffer and LoadTextureFromFile
    // modelPath: path relative to working dir, e.g. L"Assets/Models/FBX/box.fbx"
    // defaultTexture: used when mesh has no diffuse texture or texture load fails
    static std::vector<LoadedMeshPart> Load(
        CommandList& commandList,
        const std::wstring& modelPath,
        const Texture& defaultTexture);
};
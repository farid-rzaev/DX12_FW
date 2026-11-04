#include "PipelineStage.h"

#include <Framework/Application.h>
#include <Framework/CommandList.h>
#include <Framework/DynamicDescriptorHeap.h>
#include <Framework/Material/Resource.h>
#include <Framework/Material/Texture.h>
#include <Framework/Material/UploadBuffer.h>
#include <Framework/Stages/StageViews.h>

#include <Framework/3RD_Party/Helpers.h>

#include <d3dcompiler.h>
#include <d3d12shader.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

// =====================================================================================
//									   HELPERs 
// =====================================================================================

namespace
{
    using ReflectedResource = PipelineStage::ReflectedResource;

    // Marks structured buffers whose element layout reflection couldn't provide.
    const char* const kUnavailableLayout = "<unavailable-structured-layout>";

    std::string Narrow(const std::wstring& s)
    {
        return std::string(s.begin(), s.end());
    }

    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    ComPtr<ID3DBlob> LoadShader(const std::wstring& shaderBytecodeDir, const std::wstring& fileName)
    {
        const std::wstring path = shaderBytecodeDir + L"\\" + fileName;

        ComPtr<ID3DBlob> blob;
        if (FAILED(D3DReadFileToBlob(path.c_str(), &blob)))
        {
            throw std::runtime_error("PipelineStage: failed to load shader bytecode '" + Narrow(path) + "'.");
        }
        return blob;
    }

    struct ShaderReflectionInfo
    {
        bool     HasVertexInputs    = false;
        uint32_t ThreadGroupSize[3] = { 1, 1, 1 };
    };

    // Recursive fingerprint of a reflected type (class, scalar type, dimensions, offset, members).
    std::string TypeLayout(ID3D12ShaderReflectionType* type)
    {
        D3D12_SHADER_TYPE_DESC typeDesc = {};
        ThrowIfFailed(type->GetDesc(&typeDesc));

        std::string layout = std::to_string(typeDesc.Class)    + "," +
                             std::to_string(typeDesc.Type)     + "," +
                             std::to_string(typeDesc.Rows)     + "," +
                             std::to_string(typeDesc.Columns)  + "," +
                             std::to_string(typeDesc.Elements) + "," +
                             std::to_string(typeDesc.Offset)   + "{";

        for (UINT i = 0; i < typeDesc.Members; ++i)
        {
            layout += std::string(type->GetMemberTypeName(i)) + TypeLayout(type->GetMemberTypeByIndex(i));
        }

        return layout + "}";
    }

    // Fingerprint of a cbuffer / structured buffer declaration. Used to check that a resource
    // declared by both VS and PS under the same name really is the same declaration.
    std::string DeclarationLayout(ID3D12ShaderReflection* reflection, const D3D12_SHADER_INPUT_BIND_DESC& bind)
    {
        std::string layout = std::to_string(bind.ReturnType) + ":" + std::to_string(bind.uFlags) + ":";

        if (bind.Type == D3D_SIT_CBUFFER)
        {
            ID3D12ShaderReflectionConstantBuffer* constantBuffer = reflection->GetConstantBufferByName(bind.Name);

            D3D12_SHADER_BUFFER_DESC bufferDesc = {};
            ThrowIfFailed(constantBuffer->GetDesc(&bufferDesc), "PipelineStage: failed to reflect cbuffer layout.");

            for (UINT i = 0; i < bufferDesc.Variables; ++i)
            {
                ID3D12ShaderReflectionVariable* variable = constantBuffer->GetVariableByIndex(i);

                D3D12_SHADER_VARIABLE_DESC variableDesc = {};
                ThrowIfFailed(variable->GetDesc(&variableDesc));

                layout += std::string(variableDesc.Name)            + ":" +
                          std::to_string(variableDesc.StartOffset) + ":" +
                          std::to_string(variableDesc.Size)        + ":" +
                          TypeLayout(variable->GetType());
            }
        }
        else if (bind.Type == D3D_SIT_STRUCTURED || bind.Type == D3D_SIT_UAV_RWSTRUCTURED)
        {
            // Structured buffers are exposed as a constant buffer of the same name holding the element type.
            ID3D12ShaderReflectionConstantBuffer* elementInfo = reflection->GetConstantBufferByName(bind.Name);

            D3D12_SHADER_BUFFER_DESC bufferDesc = {};
            if (SUCCEEDED(elementInfo->GetDesc(&bufferDesc)))
            {
                for (UINT i = 0; i < bufferDesc.Variables; ++i)
                {
                    layout += TypeLayout(elementInfo->GetVariableByIndex(i)->GetType());
                }
            }
            else
            {
                layout += kUnavailableLayout;
            }
        }

        return layout;
    }

    // D3DReflect handles SM 5.x bytecode (FXC). SM 6.x (DXC) needs IDxcUtils::CreateReflection here.
    ShaderReflectionInfo ReflectShader(ID3DBlob* blob,
                                       D3D12_SHADER_VISIBILITY visibility,
                                       std::vector<ReflectedResource>& inOutResources)
    {
        ComPtr<ID3D12ShaderReflection> reflection;
        ThrowIfFailed(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection)));

        D3D12_SHADER_DESC shaderDesc = {};
        ThrowIfFailed(reflection->GetDesc(&shaderDesc));

        ShaderReflectionInfo info;

        for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC signatureDesc = {};
            ThrowIfFailed(reflection->GetInputParameterDesc(i, &signatureDesc));

            // System values (SV_VertexID, ...) don't come from the input assembler.
            if (signatureDesc.SystemValueType == D3D_NAME_UNDEFINED)
            {
                info.HasVertexInputs = true;
            }
        }

        reflection->GetThreadGroupSize(&info.ThreadGroupSize[0], &info.ThreadGroupSize[1], &info.ThreadGroupSize[2]);

        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bind = {};
            ThrowIfFailed(reflection->GetResourceBindingDesc(i, &bind));

            if (bind.BindCount == 0)
            {
                throw std::runtime_error(std::string("PipelineStage: unbounded resource array '") + bind.Name + "' is not supported.");
            }

            uint32_t byteSize = 0;
            if (bind.Type == D3D_SIT_CBUFFER)
            {
                D3D12_SHADER_BUFFER_DESC bufferDesc = {};
                ThrowIfFailed(reflection->GetConstantBufferByName(bind.Name)->GetDesc(&bufferDesc),
                              "PipelineStage: failed to reflect cbuffer layout.");
                byteSize = bufferDesc.Size;
            }
            else if (bind.Type == D3D_SIT_STRUCTURED || bind.Type == D3D_SIT_UAV_RWSTRUCTURED)
            {
                byteSize = bind.NumSamples;   // holds the stride for structured buffers
            }

            const ReflectedResource current = { bind.Name, bind.Type, bind.Dimension,
                                                bind.BindPoint, bind.BindCount, bind.Space,
                                                byteSize, visibility, DeclarationLayout(reflection.Get(), bind) };

            // Resources are shared between stages by NAME only. Different names on the same
            // register (e.g. VS b0 + PS b0) stay separate, stage-visible bindings.
            auto existing = std::find_if(inOutResources.begin(), inOutResources.end(),
                [&](const ReflectedResource& resource)
                {
                    return resource.Name == current.Name;
                });

            if (existing == inOutResources.end())
            {
                inOutResources.push_back(current);
                continue;
            }

            const bool isSameDeclaration = existing->Type      == current.Type      &&
                                           existing->Dimension == current.Dimension &&
                                           existing->BindPoint == current.BindPoint &&
                                           existing->Space     == current.Space     &&
                                           existing->BindCount == current.BindCount &&
                                           existing->ByteSize  == current.ByteSize  &&
                                           existing->Layout    == current.Layout;
            if (!isSameDeclaration)
            {
                throw std::runtime_error("PipelineStage: incompatible shared declaration '" + current.Name + "'.");
            }

            if (current.Layout.find(kUnavailableLayout) != std::string::npos)
            {
                throw std::runtime_error("PipelineStage: cannot validate shared structured buffer layout '" + current.Name + "'.");
            }

            existing->Visibility = D3D12_SHADER_VISIBILITY_ALL;
        }

        return info;
    }

    D3D12_STATIC_SAMPLER_DESC SamplerFromName(const ReflectedResource& resource)
    {
        const std::string lowerName = ToLower(resource.Name);

        const bool isPoint  = lowerName == "pointclampsampler"  || lowerName == "pointwrapsampler";
        const bool isLinear = lowerName == "linearclampsampler" || lowerName == "linearwrapsampler";
        const bool isWrap   = lowerName == "pointwrapsampler"   || lowerName == "linearwrapsampler";

        if ((!isPoint && !isLinear) || resource.BindCount != 1)
        {
            throw std::runtime_error("PipelineStage: declare an explicit static sampler for '" + resource.Name + "'.");
        }

        const D3D12_TEXTURE_ADDRESS_MODE address = isWrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

        CD3DX12_STATIC_SAMPLER_DESC sampler(
            resource.BindPoint,
            isPoint ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            address, address, address);

        sampler.RegisterSpace    = resource.Space;
        sampler.ShaderVisibility = resource.Visibility;
        return sampler;
    }

    D3D_ROOT_SIGNATURE_VERSION HighestRootSignatureVersion(ID3D12Device* device)
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        return featureData.HighestVersion;
    }

    D3D12_RESOURCE_STATES SRVState(bool isCompute, D3D12_SHADER_VISIBILITY visibility)
    {
        // Compute command lists reject PIXEL_SHADER_RESOURCE.
        if (isCompute)                                   return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (visibility == D3D12_SHADER_VISIBILITY_PIXEL) return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (visibility == D3D12_SHADER_VISIBILITY_ALL)   return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
}

// =====================================================================================
//									   PipelineStage 
// =====================================================================================

void PipelineStage::Create(const std::wstring& shaderBytecodeDir, const GraphicsStageDesc& desc)
{
    // A failed rebuild must not leave the old PSO paired with new slot metadata.
    m_PipelineState.Reset();
    m_Name      = desc.Name;
    m_IsCompute = false;

    ThrowIfFailed(!desc.VS.empty(), "PipelineStage: graphics stage requires a VS.");
    ThrowIfFailed(desc.PS.empty() || !desc.RTVFormats.empty(),
                  "PipelineStage: RTVFormats are empty - call GraphicsStageDesc::SetOutputs or set them explicitly.");
    ThrowIfFailed(desc.RTVFormats.size() <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT, "PipelineStage: too many RTVFormats.");

    ComPtr<ID3DBlob> vs = LoadShader(shaderBytecodeDir, desc.VS);
    ComPtr<ID3DBlob> ps = desc.PS.empty() ? nullptr : LoadShader(shaderBytecodeDir, desc.PS);

    std::vector<ReflectedResource> resources;
    const ShaderReflectionInfo vsInfo = ReflectShader(vs.Get(), D3D12_SHADER_VISIBILITY_VERTEX, resources);
    if (ps)
    {
        ReflectShader(ps.Get(), D3D12_SHADER_VISIBILITY_PIXEL, resources);
    }

    // TODO: reflect the VS input signature into an input layout for mesh passes.
    if (vsInfo.HasVertexInputs)
    {
        throw std::runtime_error("PipelineStage '" + Narrow(m_Name) +
                                 "': VS has vertex attributes; input-layout stages are not supported yet.");
    }

    BuildRootSignature(resources, desc.StaticSamplers,
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC           Samples;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC            Blend;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER            Rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1        DepthStencil;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    } stream;

    CD3DX12_DEPTH_STENCIL_DESC1 depthStencil(D3D12_DEFAULT);
    depthStencil.DepthEnable    = desc.Depth.Enabled ? TRUE : FALSE;
    depthStencil.DepthWriteMask = desc.Depth.Write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc      = desc.Depth.Func;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = (UINT)desc.RTVFormats.size();
    for (size_t i = 0; i < desc.RTVFormats.size(); ++i)
    {
        rtvFormats.RTFormats[i] = desc.RTVFormats[i];
    }

    stream.pRootSignature        = m_RootSignature.GetRootSignature().Get();
    stream.PrimitiveTopologyType = desc.Topology;
    stream.VS                    = CD3DX12_SHADER_BYTECODE(vs.Get());
    stream.PS                    = ps ? CD3DX12_SHADER_BYTECODE(ps.Get()) : CD3DX12_SHADER_BYTECODE(nullptr, 0);
    stream.Samples               = desc.Samples;
    stream.Blend                 = desc.Blend;
    stream.Rasterizer            = desc.Rasterizer;
    stream.DepthStencil          = depthStencil;
    stream.DSVFormat             = desc.Depth.Format;
    stream.RTVFormats            = rtvFormats;

    auto device = Application::Get().GetDevice();

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = { sizeof(PipelineStateStream), &stream };
    ThrowIfFailed(device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)));
    m_PipelineState->SetName(m_Name.c_str());
}

void PipelineStage::Create(const std::wstring& shaderBytecodeDir, const ComputeStageDesc& desc)
{
    // A failed rebuild must not leave the old PSO paired with new slot metadata.
    m_PipelineState.Reset();
    m_Name      = desc.Name;
    m_IsCompute = true;

    ThrowIfFailed(!desc.CS.empty(), "PipelineStage: compute stage requires a CS.");

    ComPtr<ID3DBlob> cs = LoadShader(shaderBytecodeDir, desc.CS);

    std::vector<ReflectedResource> resources;
    const ShaderReflectionInfo csInfo = ReflectShader(cs.Get(), D3D12_SHADER_VISIBILITY_ALL, resources);
    std::copy(std::begin(csInfo.ThreadGroupSize), std::end(csInfo.ThreadGroupSize), m_ThreadGroupSize);

    BuildRootSignature(resources, desc.StaticSamplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS             CS;
    } stream;

    stream.pRootSignature = m_RootSignature.GetRootSignature().Get();
    stream.CS             = CD3DX12_SHADER_BYTECODE(cs.Get());

    auto device = Application::Get().GetDevice();

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = { sizeof(PipelineStateStream), &stream };
    ThrowIfFailed(device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)));
    m_PipelineState->SetName(m_Name.c_str());
}

void PipelineStage::BuildRootSignature(const std::vector<ReflectedResource>& resources,
                                       const std::vector<D3D12_STATIC_SAMPLER_DESC>& explicitSamplers,
                                       D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    m_Slots.clear();
    m_BindingDescs.clear();

    std::vector<CD3DX12_ROOT_PARAMETER1>   rootParameters;
    std::vector<CD3DX12_DESCRIPTOR_RANGE1> ranges;
    std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
    uint32_t                               rootCostDwords = 0;

    // Root parameters keep pointers into this vector - it must never reallocate.
    ranges.reserve(resources.size());

    for (const ReflectedResource& resource : resources)
    {
        // Samplers -> static samplers (explicit desc by register/space/visibility, otherwise a named preset).
        if (resource.Type == D3D_SIT_SAMPLER)
        {
            for (UINT i = 0; i < resource.BindCount; ++i)
            {
                auto explicitSampler = std::find_if(explicitSamplers.begin(), explicitSamplers.end(),
                    [&](const D3D12_STATIC_SAMPLER_DESC& sampler)
                    {
                        return sampler.ShaderRegister == resource.BindPoint + i &&
                               sampler.RegisterSpace  == resource.Space &&
                               (sampler.ShaderVisibility == resource.Visibility ||
                                sampler.ShaderVisibility == D3D12_SHADER_VISIBILITY_ALL);
                    });

                if (explicitSampler != explicitSamplers.end())
                {
                    staticSamplers.push_back(*explicitSampler);
                }
                else
                {
                    staticSamplers.push_back(SamplerFromName(resource));
                }
            }
            continue;
        }

        SlotKind kind;
        switch (resource.Type)
        {
        case D3D_SIT_CBUFFER:
            kind = SlotKind::RootCBV;
            break;
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_TEXTURE:
        case D3D_SIT_TBUFFER:
        case D3D_SIT_BYTEADDRESS:
            kind = SlotKind::TableSRV;
            break;
        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
            kind = SlotKind::TableUAV;
            break;
        default:
            throw std::runtime_error("PipelineStage: unsupported resource type for '" + resource.Name + "'.");
        }

        const Slot slot = { kind, (uint32_t)rootParameters.size(), resource.BindCount, resource.ByteSize,
                            resource.Dimension, resource.Visibility, (uint32_t)m_BindingDescs.size() };
        m_Slots.emplace(resource.Name, slot);

        for (UINT i = 0; i < resource.BindCount; ++i)
        {
            m_BindingDescs.push_back({ resource.Name, i });
        }

        CD3DX12_ROOT_PARAMETER1 rootParameter;
        if (kind == SlotKind::RootCBV)
        {
            if (resource.BindCount != 1)
            {
                throw std::runtime_error("PipelineStage: cbuffer arrays are not supported yet ('" + resource.Name + "').");
            }

            rootParameter.InitAsConstantBufferView(resource.BindPoint, resource.Space, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, resource.Visibility);
            rootCostDwords += 2;
        }
        else
        {
            // One stage-visible table per resource, so unrelated VS/PS resources may both use e.g. t0.
            // Arrays occupy one contiguous range of the table.
            const D3D12_DESCRIPTOR_RANGE_TYPE rangeType = kind == SlotKind::TableSRV ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                                                                                     : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges.emplace_back(rangeType, resource.BindCount, resource.BindPoint, resource.Space);

            rootParameter.InitAsDescriptorTable(1, &ranges.back(), resource.Visibility);
            rootCostDwords += 1;
        }
        rootParameters.push_back(rootParameter);
    }

    // 64 DWORDs is the D3D12 limit; DynamicDescriptorHeap tracks tables in a 32-bit root-index mask.
    if (rootCostDwords > 64 || rootParameters.size() > 32)
    {
        throw std::runtime_error("PipelineStage '" + Narrow(m_Name) + "': root signature exceeds D3D12/framework limits.");
    }

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1((UINT)rootParameters.size(), rootParameters.data(),
                                      (UINT)staticSamplers.size(), staticSamplers.data(), flags);

    auto device = Application::Get().GetDevice();

    // SetRootSignatureDesc deep-copies parameters, ranges and samplers.
    m_RootSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, HighestRootSignatureVersion(device.Get()));
    m_RootSignature.GetRootSignature()->SetName(m_Name.c_str());
}

void PipelineStage::Bind(CommandList& commandList) const
{
    ThrowIfFailed((bool)m_PipelineState, "PipelineStage: Create must be called before Bind.");

    const D3D12_COMMAND_LIST_TYPE commandListType = commandList.GetCommandListType();
    if (commandListType == D3D12_COMMAND_LIST_TYPE_COPY ||
        (!m_IsCompute && commandListType != D3D12_COMMAND_LIST_TYPE_DIRECT))
    {
        throw std::runtime_error("PipelineStage '" + Narrow(m_Name) + "': incompatible command list type.");
    }

    commandList.SetPipelineState(m_PipelineState);
    if (m_IsCompute)
    {
        commandList.SetComputeRootSignature(m_RootSignature);
    }
    else
    {
        commandList.SetGraphicsRootSignature(m_RootSignature);
    }

    // Set*RootSignature skips parsing when the root signature didn't change. Re-parse anyway, so
    // descriptors staged by a previous Bind of this stage can't satisfy this one.
    for (auto& dynamicDescriptorHeap : commandList.m_DynamicDescriptorHeap)
    {
        dynamicDescriptorHeap->ParseRootSignature(m_RootSignature);
    }

    commandList.StartStage(this, m_IsCompute, m_BindingDescs);
}

bool PipelineStage::HasBinding(const char* name) const
{
    return m_Slots.find(name) != m_Slots.end();
}

const PipelineStage::Slot* PipelineStage::FindSlot(const char* name, SlotKind expected) const
{
    auto iter = m_Slots.find(name);
    if (iter == m_Slots.end())
    {
        throw std::runtime_error("PipelineStage '" + Narrow(m_Name) + "': unknown binding '" + name +
                                 "'. Use TrySet* for resources the shader may not declare.");
    }

    if (iter->second.Kind != expected)
    {
        throw std::runtime_error("PipelineStage '" + Narrow(m_Name) + "': wrong Set* call for '" + name + "'.");
    }

    return &iter->second;
}

void PipelineStage::RequireActive(const CommandList& commandList) const
{
    if (commandList.m_ActiveStage != this)
    {
        throw std::runtime_error("PipelineStage '" + Narrow(m_Name) + "': Bind this stage before setting resources.");
    }
}

void PipelineStage::SetCBImpl(CommandList& commandList, const char* name, size_t sizeInBytes, const void* data) const
{
    RequireActive(commandList);
    const Slot& slot = *FindSlot(name, SlotKind::RootCBV);

    if (sizeInBytes != slot.ByteSize)
    {
        throw std::runtime_error("PipelineStage: C++/HLSL constant buffer size mismatch for '" + std::string(name) +
                                 "' (C++ " + std::to_string(sizeInBytes) + " bytes, HLSL " + std::to_string(slot.ByteSize) + " bytes).");
    }

    if (m_IsCompute)
    {
        commandList.SetComputeDynamicConstantBuffer(slot.RootIndex, sizeInBytes, data);
    }
    else
    {
        commandList.SetGraphicsDynamicConstantBuffer(slot.RootIndex, sizeInBytes, data);
    }

    commandList.m_StageBindings.at(slot.BindingIndex).Bound = true;
}

void PipelineStage::SetSRV(CommandList& commandList, const char* name, const Resource& resource,
                           const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc, uint32_t arrayIndex) const
{
    RequireActive(commandList);
    const Slot& slot = *FindSlot(name, SlotKind::TableSRV);

    if (arrayIndex >= slot.ArraySize || !resource.IsValid())
    {
        throw std::runtime_error("PipelineStage: invalid SRV resource or array index for '" + std::string(name) + "'.");
    }

    const D3D12_RESOURCE_DESC resourceDesc = resource.GetD3D12ResourceDesc();
    StageViews::ValidateResource(resourceDesc, slot.Dimension);

    const D3D12_SHADER_RESOURCE_VIEW_DESC view = srvDesc ? *srvDesc : StageViews::DefaultSRV(resource, slot.Dimension);
    if ((UINT)view.ViewDimension != (UINT)slot.Dimension)
    {
        throw std::runtime_error("PipelineStage: SRV dimension does not match the shader for '" + std::string(name) + "'.");
    }

    if (view.ViewDimension == D3D12_SRV_DIMENSION_BUFFER && slot.ByteSize != 0 && view.Buffer.StructureByteStride != slot.ByteSize)
    {
        throw std::runtime_error("PipelineStage: structured buffer stride mismatch for '" + std::string(name) + "'.");
    }

    const StageSubresourceRange subresources = StageViews::Subresources(resourceDesc, view);

    // Textures cache one view per desc; buffers only cache their default view.
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor;
    if (dynamic_cast<const Texture*>(&resource))
    {
        descriptor = resource.GetShaderResourceView(&view);
    }
    else if (!srvDesc)
    {
        descriptor = resource.GetShaderResourceView(nullptr);
    }
    else
    {
        descriptor = CreateTransientSRV(commandList, resource.GetD3D12Resource().Get(), view);
    }

    commandList.m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(slot.RootIndex, arrayIndex, 1, descriptor);

    D3D12_HEAP_PROPERTIES heapProperties = {};
    ThrowIfFailed(resource.GetD3D12Resource()->GetHeapProperties(&heapProperties, nullptr));

    commandList.RecordStageResource(slot.BindingIndex + arrayIndex, resource.GetD3D12Resource().Get(), subresources,
                                    SRVState(m_IsCompute, slot.Visibility), heapProperties.Type == D3D12_HEAP_TYPE_UPLOAD);
}

bool PipelineStage::TrySetSRV(CommandList& commandList, const char* name, const Resource& resource,
                              const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc, uint32_t arrayIndex) const
{
    RequireActive(commandList);
    if (!HasBinding(name))
    {
        return false;
    }

    SetSRV(commandList, name, resource, srvDesc, arrayIndex);
    return true;
}

void PipelineStage::SetUAV(CommandList& commandList, const char* name, const Resource& resource,
                           const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc, uint32_t arrayIndex) const
{
    RequireActive(commandList);
    const Slot& slot = *FindSlot(name, SlotKind::TableUAV);

    if (arrayIndex >= slot.ArraySize || !resource.IsValid())
    {
        throw std::runtime_error("PipelineStage: invalid UAV resource or array index for '" + std::string(name) + "'.");
    }

    const D3D12_RESOURCE_DESC resourceDesc = resource.GetD3D12ResourceDesc();
    if (!(resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
    {
        throw std::runtime_error("PipelineStage: resource bound to '" + std::string(name) + "' does not allow UAV access.");
    }
    StageViews::ValidateResource(resourceDesc, slot.Dimension);

    const D3D12_UNORDERED_ACCESS_VIEW_DESC view = uavDesc ? *uavDesc : StageViews::DefaultUAV(resource, slot.Dimension);
    if (view.ViewDimension != StageViews::UAVDimensionFor(slot.Dimension))
    {
        throw std::runtime_error("PipelineStage: UAV dimension does not match the shader for '" + std::string(name) + "'.");
    }

    if (view.ViewDimension == D3D12_UAV_DIMENSION_BUFFER && slot.ByteSize != 0 && view.Buffer.StructureByteStride != slot.ByteSize)
    {
        throw std::runtime_error("PipelineStage: structured buffer stride mismatch for '" + std::string(name) + "'.");
    }

    const StageSubresourceRange subresources = StageViews::Subresources(resourceDesc, view);

    // Textures cache one view per desc; buffers only cache their default view (incl. the counter for structured buffers).
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor;
    if (dynamic_cast<const Texture*>(&resource))
    {
        descriptor = resource.GetUnorderedAccessView(&view);
    }
    else if (!uavDesc)
    {
        descriptor = resource.GetUnorderedAccessView(nullptr);
    }
    else
    {
        descriptor = CreateTransientUAV(commandList, resource.GetD3D12Resource().Get(), view);
    }

    commandList.m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(slot.RootIndex, arrayIndex, 1, descriptor);

    commandList.RecordStageResource(slot.BindingIndex + arrayIndex, resource.GetD3D12Resource().Get(), subresources,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

bool PipelineStage::TrySetUAV(CommandList& commandList, const char* name, const Resource& resource,
                              const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc, uint32_t arrayIndex) const
{
    RequireActive(commandList);
    if (!HasBinding(name))
    {
        return false;
    }

    SetUAV(commandList, name, resource, uavDesc, arrayIndex);
    return true;
}

void PipelineStage::UploadAndSetSRVImpl(CommandList& commandList, const char* name,
                                        size_t numElements, size_t elementSize, const void* data) const
{
    RequireActive(commandList);
    const Slot& slot = *FindSlot(name, SlotKind::TableSRV);

    if (slot.Dimension != D3D_SRV_DIMENSION_BUFFER || slot.ByteSize == 0 || slot.ArraySize != 1)
    {
        throw std::runtime_error("PipelineStage: UploadAndSetSRV requires a non-array StructuredBuffer ('" + std::string(name) + "').");
    }

    if (elementSize != slot.ByteSize)
    {
        throw std::runtime_error("PipelineStage: structured buffer stride mismatch for '" + std::string(name) + "'.");
    }

    if (numElements == 0 || numElements > UINT_MAX || data == nullptr)
    {
        throw std::runtime_error("PipelineStage: UploadAndSetSRV requires non-empty data ('" + std::string(name) + "').");
    }

    // A structured buffer view starts at a multiple of the stride, so over-allocate by one element
    // and start at the first stride-aligned element inside the allocation.
    UploadBuffer& uploadBuffer   = *commandList.m_UploadBuffer;
    const size_t  bufferSize     = numElements * elementSize;
    const size_t  allocationSize = bufferSize + elementSize;
    if (Math::AlignUp(allocationSize, 16) > uploadBuffer.GetPageSize())
    {
        throw std::runtime_error("PipelineStage: UploadAndSetSRV data for '" + std::string(name) + "' is " + std::to_string(bufferSize) +
                                 " bytes, the upload page size is " + std::to_string(uploadBuffer.GetPageSize()) + ".");
    }

    const UploadBuffer::Allocation allocation   = uploadBuffer.Allocate(allocationSize, 16);
    const size_t                   firstElement = Math::DivideByMultiple(allocation.Offset, elementSize);
    memcpy(static_cast<uint8_t*>(allocation.CPU) + (firstElement * elementSize - allocation.Offset), data, bufferSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement        = firstElement;
    srvDesc.Buffer.NumElements         = static_cast<UINT>(numElements);
    srvDesc.Buffer.StructureByteStride = static_cast<UINT>(elementSize);

    const D3D12_CPU_DESCRIPTOR_HANDLE descriptor = CreateTransientSRV(commandList, allocation.Resource, srvDesc);
    commandList.m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(slot.RootIndex, 0, 1, descriptor);

    // The upload page is owned by the command list and stays alive until it is reset.
    commandList.RecordStageResource(slot.BindingIndex, allocation.Resource, StageSubresourceRange(), D3D12_RESOURCE_STATE_GENERIC_READ, true);
}

D3D12_CPU_DESCRIPTOR_HANDLE PipelineStage::CreateTransientSRV(CommandList& commandList, ID3D12Resource* resource,
                                                              const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) const
{
    auto& app        = Application::Get();
    auto  descriptor = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    app.GetDevice()->CreateShaderResourceView(resource, &srvDesc, descriptor.GetDescriptorHandle());

    const D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor.GetDescriptorHandle();
    commandList.m_StageDescriptors.push_back(std::move(descriptor));
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE PipelineStage::CreateTransientUAV(CommandList& commandList, ID3D12Resource* resource,
                                                              const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc) const
{
    auto& app        = Application::Get();
    auto  descriptor = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    app.GetDevice()->CreateUnorderedAccessView(resource, nullptr, &uavDesc, descriptor.GetDescriptorHandle());

    const D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor.GetDescriptorHandle();
    commandList.m_StageDescriptors.push_back(std::move(descriptor));
    return handle;
}

void PipelineStage::DispatchThreads(CommandList& commandList, uint32_t threadsX, uint32_t threadsY, uint32_t threadsZ) const
{
    RequireActive(commandList);
    ThrowIfFailed(m_IsCompute, "PipelineStage: DispatchThreads requires a compute stage.");

    commandList.Dispatch(Math::DivideByMultiple(threadsX, m_ThreadGroupSize[0]),
                         Math::DivideByMultiple(threadsY, m_ThreadGroupSize[1]),
                         Math::DivideByMultiple(threadsZ, m_ThreadGroupSize[2]));
}

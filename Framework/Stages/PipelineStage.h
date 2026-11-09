#pragma once

#include <Framework/3RD_Party/Defines.h>
#include <Framework/RootSignature.h>
#include <Framework/Stages/StageBindingState.h>
#include <Framework/Stages/StageDesc.h>

#include <wrl.h>

#include <string>
#include <unordered_map>
#include <vector>

class CommandList;
class Resource;

// A graphics or compute pipeline stage built from a stage desc + shader reflection.
//
// The root signature is synthesized from the resources the shaders actually use:
//   - each cbuffer              -> inline root CBV                         (SetCB)
//   - each buffer / texture SRV -> its own stage-visible descriptor table  (SetSRV / UploadAndSetSRV)
//   - each UAV                  -> its own stage-visible descriptor table  (SetUAV)
//   - samplers                  -> static samplers
//
// Mesh passes take their input layout from the desc; reflection validates it against the
// VS input signature (semantic + index present, enough components, matching base type).
//
// Resources are bound BY NAME, as written in HLSL:
//   - Set* throws for names the shader doesn't declare and for the wrong Set* kind.
//   - TrySet* returns false for undeclared names (resources the compiler may strip).
//   - Every declared resource must be bound before Draw/Dispatch - the command list checks it.
class DX12_FW_API PipelineStage
{
public:
    PipelineStage() = default;

    // RootSignature owns raw arrays and has no copy semantics.
    PipelineStage(const PipelineStage&) = delete;
    PipelineStage& operator=(const PipelineStage&) = delete;

    // Build root signature + PSO. Call once, e.g. in LoadContent.
    void Create(const std::wstring& shaderBytecodeDir, const GraphicsStageDesc& desc);
    void Create(const std::wstring& shaderBytecodeDir, const ComputeStageDesc& desc);
    // Compute stage from bytecode compiled into a header (desc.CSBytecode).
    void Create(const ComputeStageDesc& desc);

    // Set PSO + root signature (+ primitive topology for graphics stages) and start a fresh set of
    // required bindings on the command list.
    // Bindings persist across draws until the next Bind (or a manual PSO/root signature change).
    void Bind(CommandList& commandList) const;

    // Upload a dynamic constant buffer. sizeof(T) is validated against the HLSL cbuffer size.
    template<typename T>
    void SetCB(CommandList& commandList, const char* name, const T& data) const
    {
        SetCBImpl(commandList, name, sizeof(T), &data);
    }

    template<typename T>
    bool TrySetCB(CommandList& commandList, const char* name, const T& data) const
    {
        RequireActive(commandList);
        if (!HasBinding(name))
        {
            return false;
        }

        SetCB(commandList, name, data);
        return true;
    }

    // Bind an SRV. With srvDesc == nullptr a default view matching the shader dimension is
    // created (TextureCube resources get a cube view, StructuredBuffers a structured view).
    // Only the subresources selected by the view are transitioned.
    void SetSRV(CommandList& commandList, const char* name, const Resource& resource,
                const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr, uint32_t arrayIndex = 0) const;
    bool TrySetSRV(CommandList& commandList, const char* name, const Resource& resource,
                   const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr, uint32_t arrayIndex = 0) const;

    // Bind a UAV. With uavDesc == nullptr mip 0 of the resource is used.
    void SetUAV(CommandList& commandList, const char* name, const Resource& resource,
                const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr, uint32_t arrayIndex = 0) const;
    bool TrySetUAV(CommandList& commandList, const char* name, const Resource& resource,
                   const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr, uint32_t arrayIndex = 0) const;

    // Bind a null texture UAV to a declared output the shader won't write this dispatch
    // (e.g. unused mip outputs). Counts as bound; no resource, no barrier.
    void SetNullUAV(CommandList& commandList, const char* name, uint32_t arrayIndex = 0) const;

    // Copy CPU data into a transient upload-heap buffer and bind it as a structured SRV.
    // sizeof(T) is validated against the reflected HLSL stride.
    template<typename T>
    void UploadAndSetSRV(CommandList& commandList, const char* name, const std::vector<T>& data) const
    {
        UploadAndSetSRVImpl(commandList, name, data.size(), sizeof(T), data.data());
    }

    // Compute only: dispatch enough thread groups to cover the given number of
    // threads, using the shader's reflected [numthreads].
    void DispatchThreads(CommandList& commandList, uint32_t threadsX, uint32_t threadsY = 1, uint32_t threadsZ = 1) const;

    // Draw(3) for full-screen passes: the VS turns SV_VertexID 0..2 into one triangle that
    // covers the whole viewport, so no vertex buffer is bound and the vertex count is part of
    // the VS contract, not of the call site. Throws for compute stages and for stages whose VS
    // takes vertex inputs (known from reflection - mesh passes draw through Mesh::Draw).
    void DrawFullscreenTriangle(CommandList& commandList) const;

    bool IsCompute() const { return m_IsCompute; }

    const RootSignature& GetRootSignature() const { return m_RootSignature; }
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState() const { return m_PipelineState; }

    struct ReflectedResource
    {
        std::string             Name;
        D3D_SHADER_INPUT_TYPE   Type;
        D3D_SRV_DIMENSION       Dimension;
        uint32_t                BindPoint;
        uint32_t                BindCount;
        uint32_t                Space;
        uint32_t                ByteSize;     // cbuffer size, or StructuredBuffer stride
        D3D12_SHADER_VISIBILITY Visibility;
        std::string             Layout;       // reflected declaration fingerprint (for VS/PS sharing checks)
    };

private:
    enum class SlotKind { RootCBV, TableSRV, TableUAV };

    struct Slot
    {
        SlotKind                Kind;
        uint32_t                RootIndex;
        uint32_t                ArraySize;
        uint32_t                ByteSize;
        D3D_SRV_DIMENSION       Dimension;
        D3D12_SHADER_VISIBILITY Visibility;
        uint32_t                BindingIndex;   // first entry in m_BindingDescs
    };

    void CreateCompute(const ComputeStageDesc& desc, ID3DBlob* cs);

    void BuildRootSignature(const std::vector<ReflectedResource>& resources,
                            const std::vector<D3D12_STATIC_SAMPLER_DESC>& explicitSamplers,
                            D3D12_ROOT_SIGNATURE_FLAGS flags);

    bool HasBinding(const char* name) const;

    // Throws for unknown names and for the wrong binding kind.
    const Slot* FindSlot(const char* name, SlotKind expected) const;

    // Throws if this stage isn't the one currently bound on the command list.
    void RequireActive(const CommandList& commandList) const;

    void SetCBImpl(CommandList& commandList, const char* name, size_t sizeInBytes, const void* data) const;
    void UploadAndSetSRVImpl(CommandList& commandList, const char* name,
                             size_t numElements, size_t elementSize, const void* data) const;

    // Descriptor for a view no resource cache provides. Lives until the command list is reset.
    D3D12_CPU_DESCRIPTOR_HANDLE CreateTransientSRV(CommandList& commandList, ID3D12Resource* resource,
                                                   const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CreateTransientUAV(CommandList& commandList, ID3D12Resource* resource,
                                                   const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc) const;

    std::wstring                                m_Name;
    bool                                        m_IsCompute          = false;
    uint32_t                                    m_ThreadGroupSize[3] = { 1, 1, 1 };
    D3D_PRIMITIVE_TOPOLOGY                      m_Topology           = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;   // graphics only
    bool                                        m_HasVertexInputs    = false;   // reflected VS input signature (graphics only)
    RootSignature                               m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    std::unordered_map<std::string, Slot>       m_Slots;
    std::vector<StageBindingDesc>               m_BindingDescs;   // one per resource array element
};

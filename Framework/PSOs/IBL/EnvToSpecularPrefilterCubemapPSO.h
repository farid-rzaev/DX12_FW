#pragma once

// IBL Specular prefilter convolution shader - builds Specular Prefilter Cubemap from Environment Cubemap

#include <Framework/RootSignature.h>
#include <Framework/DescriptorAllocation.h>

#include <cstdint>

// Struct for CB
struct EnvToSpecularPrefilterCubemapCB
{
    uint32_t OutputSize;
    float    Roughness;
    float    _Pad0;
    float    _Pad1;
};

// I don't use scoped enums to avoid the explicit cast that is required to 
// treat these as root indices into the root signature.
namespace EnvToSpecularPrefilterCubemapRS
{
    enum
    {
        EnvToSpecularPrefilterCubemapCB,
        SrcCubemap,
        DstCubemap,
        NumRootParameters
    };
}

class Application;

class EnvToSpecularPrefilterCubemapPSO
{
public:
    EnvToSpecularPrefilterCubemapPSO();

    const RootSignature& GetRootSignature() const
    {
        return m_RootSignature;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState() const
    {
        return m_PipelineState;
    }

private:
    RootSignature m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
};
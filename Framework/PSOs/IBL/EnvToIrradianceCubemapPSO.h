#pragma once

// IBL Irradiance convolution shader - builds Irradiance Cubemap from Environment Cubemap

#include <Framework/RootSignature.h>
#include <Framework/DescriptorAllocation.h>

#include <cstdint>

// Struct for CB
struct EnvToIrradianceCubemapCB
{
    uint32_t OutputSize;
};

// I don't use scoped enums to avoid the explicit cast that is required to 
// treat these as root indices into the root signature.
namespace EnvToIrradianceCubemapRS
{
    enum
    {
        EnvToIrradianceCubemapCB,
        SrcCubemap,
        DstCubemap,
        NumRootParameters
    };
}

class Application;

class EnvToIrradianceCubemapPSO
{
public:
    EnvToIrradianceCubemapPSO();

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
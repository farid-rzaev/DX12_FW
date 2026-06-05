#pragma once

// IBL BRDF LUT shader - builds the BRDF LUT used for split-sum approximation of the specular IBL integral

#include <Framework/RootSignature.h>
#include <Framework/DescriptorAllocation.h>

#include <cstdint>

// I don't use scoped enums to avoid the explicit cast that is required to 
// treat these as root indices into the root signature.
namespace BrdfLutRS
{
    enum
    {
        DstTexture,
        NumRootParameters
    };
}

class Application;

class BrdfLutPSO
{
public:
    BrdfLutPSO();

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
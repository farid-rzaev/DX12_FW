#pragma once

#include <Framework/3RD_Party/Defines.h>

#include <Framework/3RD_Party/D3D/d3dx12.h>

#include <wrl.h>

#include <vector>

class RootSignature
{
public:
    // TODO: Add (deep) copy/move constructors and assignment operators!
    DX12_FW_API RootSignature();
    DX12_FW_API RootSignature(
        const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc, 
        D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion
    );

    virtual DX12_FW_API ~RootSignature();

    DX12_FW_API void Destroy();

    DX12_FW_API Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignature() const
    {
        return m_RootSignature;
    }

    DX12_FW_API void SetRootSignatureDesc(
        const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion
    );

    DX12_FW_API const D3D12_ROOT_SIGNATURE_DESC1& GetRootSignatureDesc() const
    {
        return m_RootSignatureDesc;
    }

    DX12_FW_API uint32_t GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const;
    DX12_FW_API uint32_t GetNumDescriptors(uint32_t rootIndex) const;

protected:

private:
    D3D12_ROOT_SIGNATURE_DESC1 m_RootSignatureDesc;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;

    // Need to know the number of descriptors per descriptor table.
    // A maximum of 32 descriptor tables are supported (since a 32-bit
    // mask is used to represent the descriptor tables in the root signature.
    uint32_t m_NumDescriptorsPerTable[32];

    // A bit mask that represents the root parameter indices that are 
    // descriptor tables for Samplers.
    uint32_t m_SamplerTableBitMask;
    // A bit mask that represents the root parameter indices that are 
    // CBV, UAV, and SRV descriptor tables.
    uint32_t m_DescriptorTableBitMask;
};
#include "EnvToIrradianceCubemapPSO.h"

// For compiled Shader bytecode - g_IBL_IrradianceConv_CS 
// "_Output/Bin/$(Platform)/$(Configuration)/$(ProjectName)/Shaders/g_IBL_IrradianceConv_CS.h"
#include <IBL_IrradianceConv_CS.h>

#include <Framework/Application.h>

#include <Framework/3RD_Party/D3D/d3dx12.h>
#include <Framework/3RD_Party/Helpers.h>


EnvToIrradianceCubemapPSO::EnvToIrradianceCubemapPSO()
{
    auto& app   = Application::Get();
    auto device = app.GetDevice();

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CD3DX12_DESCRIPTOR_RANGE1 srcTexture(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    CD3DX12_DESCRIPTOR_RANGE1 outTexture(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 rootParameters[EnvToIrradianceCubemapRS::NumRootParameters];
    rootParameters[EnvToIrradianceCubemapRS::EnvToIrradianceCubemapCB].InitAsConstants(sizeof(EnvToIrradianceCubemapCB) / 4, 0);
    rootParameters[EnvToIrradianceCubemapRS::SrcCubemap].InitAsDescriptorTable(1, &srcTexture);
    rootParameters[EnvToIrradianceCubemapRS::DstCubemap].InitAsDescriptorTable(1, &outTexture);

    CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP
    );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(EnvToIrradianceCubemapRS::NumRootParameters,
        rootParameters, 1, &linearRepeatSampler);

    m_RootSignature.SetRootSignatureDesc(rootSignatureDesc.Desc_1_1, featureData.HighestVersion);

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS CS;
    } pipelineStateStream;

    pipelineStateStream.pRootSignature = m_RootSignature.GetRootSignature().Get();
    pipelineStateStream.CS = { g_IBL_IrradianceConv_CS, sizeof(g_IBL_IrradianceConv_CS) };

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
        sizeof(PipelineStateStream), &pipelineStateStream
    };

    ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_PipelineState))); 
}
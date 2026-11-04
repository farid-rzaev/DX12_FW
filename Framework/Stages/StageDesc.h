#pragma once

#include <Framework/3RD_Party/D3D/d3dx12.h>
#include <Framework/Material/RenderTarget.h>

#include <stdexcept>
#include <string>
#include <vector>

// Hand-authored descriptions of pipeline stages.
//
// Only state that CANNOT be deduced from shader reflection lives here:
//   - which shaders to use
//   - output formats + sample settings (an SV_Target type says nothing about the texture format)
//   - depth / blend / raster policy
//   - optional explicit static samplers
//
// Reflection supplies bindings and group sizes; bound views supply resource-access ranges.
//
// Static samplers without an explicit desc must use one of the exact preset names
// (case insensitive): PointClampSampler, PointWrapSampler, LinearClampSampler, LinearWrapSampler.
// Any other name requires an entry in StaticSamplers (matched by register/space/visibility).

// Default: depth test disabled (typical for fullscreen post/lighting passes).
struct StageDepthPolicy
{
    bool                  Enabled = false;
    bool                  Write   = false;
    D3D12_COMPARISON_FUNC Func    = D3D12_COMPARISON_FUNC_LESS;

    // DSV format the PSO is used with. Must match the bound depth buffer
    // even when the depth test is disabled.
    DXGI_FORMAT           Format  = DXGI_FORMAT_UNKNOWN;
};

struct GraphicsStageDesc
{
    std::wstring Name;   // debug name for PIX / errors

    // Compiled shader bytecode filenames, relative to the shader bytecode dir.
    std::wstring VS;
    std::wstring PS;     // may be empty for depth-only stages

    D3D12_PRIMITIVE_TOPOLOGY_TYPE Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    std::vector<DXGI_FORMAT> RTVFormats;
    DXGI_SAMPLE_DESC         Samples    = { 1, 0 };

    StageDepthPolicy         Depth      = {};

    CD3DX12_BLEND_DESC       Blend      = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    CD3DX12_RASTERIZER_DESC  Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    std::vector<D3D12_STATIC_SAMPLER_DESC> StaticSamplers;

    // Take RTV/DSV formats and sample settings from the render target the stage draws into.
    void SetOutputs(const RenderTarget& renderTarget)
    {
        const D3D12_RT_FORMAT_ARRAY formats = renderTarget.GetRenderTargetFormats();
        RTVFormats.assign(formats.RTFormats, formats.RTFormats + formats.NumRenderTargets);
        Depth.Format = renderTarget.GetDepthStencilFormat();

        bool isFirstAttachment = true;
        for (const auto& texture : renderTarget.GetTextures())
        {
            if (!texture || !texture->IsValid())
            {
                continue;
            }

            const DXGI_SAMPLE_DESC sampleDesc = texture->GetD3D12ResourceDesc().SampleDesc;
            if (!isFirstAttachment && (sampleDesc.Count != Samples.Count || sampleDesc.Quality != Samples.Quality))
            {
                throw std::runtime_error("PipelineStage: render target attachments have different sample settings.");
            }

            Samples           = sampleDesc;
            isFirstAttachment = false;
        }
    }
};

struct ComputeStageDesc
{
    std::wstring Name;   // debug name for PIX / errors
    std::wstring CS;     // compiled shader bytecode filename

    std::vector<D3D12_STATIC_SAMPLER_DESC> StaticSamplers;
};

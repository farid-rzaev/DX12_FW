#pragma once

#include "FullscreenStage.h"

#include <Framework/Material/Mesh.h>
#include <Framework/Stages/StageDesc.h>

// Deferred-path pipeline stages (skybox, G-buffer, lighting, tonemap). Only non-deducible state
// is listed here; root signature, binding slots and static samplers come from shader reflection.
// To tweak per sample: copy the returned desc and mutate fields before PipelineStage::Create.

// Inverted cube drawn first into the HDR target. Nothing depth-tests against the HDR depth
// afterwards (lighting writes to the color-only RT), so depth stays disabled.
inline GraphicsStageDesc MakeSkyboxStageDesc(const RenderTarget& output)
{
    GraphicsStageDesc d;
    d.Name = L"Skybox";
    d.VS   = L"Skybox_VS.cso";
    d.PS   = L"Skybox_PS.cso";
    d.SetInputLayout(Mesh::GetInputLayout());
    d.SetOutputs(output);
    return d;
}

inline GraphicsStageDesc MakeGBufferStageDesc(const RenderTarget& output)
{
    GraphicsStageDesc d;
    d.Name = L"G-Buffer";
    d.VS   = L"GBuffer_VS.cso";
    d.PS   = L"GBuffer_PS.cso";
    d.SetInputLayout(Mesh::GetInputLayout());
    d.SetOutputs(output);

    d.Depth.Enabled = true;
    d.Depth.Write   = true;
    d.Depth.Func    = D3D12_COMPARISON_FUNC_LESS;
    return d;
}

inline GraphicsStageDesc MakeDeferredLightingStageDesc(const RenderTarget& output)
{
    return MakeFullscreenStageDesc(L"Deferred Lighting", L"DeferredLighting_PS.cso", output);
}

// HDR -> SDR into the window's render target.
inline GraphicsStageDesc MakeTonemapStageDesc(const RenderTarget& output)
{
    return MakeFullscreenStageDesc(L"HDR to SDR", L"HDRtoSDR_PS.cso", output);
}

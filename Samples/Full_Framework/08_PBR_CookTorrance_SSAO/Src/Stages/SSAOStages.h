#pragma once

#include <Framework/Stages/StageDesc.h>

// SSAO pipeline stages. Only non-deducible state is listed here; root signature,
// binding slots and static samplers come from shader reflection.
// To tweak per sample: copy the returned desc and mutate fields before PipelineStage::Create.

// All three are fullscreen passes driven by SV_VertexID.
static const wchar_t* const kFullscreenTriangleVS = L"DeferredLighting_VS.cso";

inline GraphicsStageDesc MakeSSAOStageDesc(const RenderTarget& output)
{
    GraphicsStageDesc d;
    d.Name = L"SSAO";
    d.VS   = kFullscreenTriangleVS;
    d.PS   = L"SSAO_PS.cso";
    d.SetOutputs(output);
    return d;
}

inline GraphicsStageDesc MakeSSAOBlurStageDesc(const RenderTarget& output)
{
    GraphicsStageDesc d;
    d.Name = L"SSAO Blur";
    d.VS   = kFullscreenTriangleVS;
    d.PS   = L"SSAOBlur_PS.cso";
    d.SetOutputs(output);
    return d;
}

// Writes AO as grayscale into the HDR target for the AO_Raw / AO_Blurred view modes.
inline GraphicsStageDesc MakeSSAODebugCopyStageDesc(const RenderTarget& output)
{
    GraphicsStageDesc d;
    d.Name = L"SSAO Debug Copy";
    d.VS   = kFullscreenTriangleVS;
    d.PS   = L"SSAODebugCopy_PS.cso";
    d.SetOutputs(output);
    return d;
}

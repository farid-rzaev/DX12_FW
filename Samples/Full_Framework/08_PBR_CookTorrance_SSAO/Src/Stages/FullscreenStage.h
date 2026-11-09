#pragma once

#include <Framework/Stages/StageDesc.h>

// The one place that defines a full-screen pass in this sample: the shared SV_VertexID vertex
// shader (no input layout, triangle list). Draw it with PipelineStage::DrawFullscreenTriangle.
// Each pass only supplies its pixel shader and output.
// To tweak per pass: mutate the returned desc (depth, blend, ...) before PipelineStage::Create.

static const wchar_t* const kFullscreenTriangleVS = L"FullscreenTriangle_VS.cso";

inline GraphicsStageDesc MakeFullscreenStageDesc(const wchar_t* name, const wchar_t* ps, const RenderTarget& output)
{
    GraphicsStageDesc d;
    d.Name = name;
    d.VS   = kFullscreenTriangleVS;
    d.PS   = ps;
    d.SetOutputs(output);

    // A single screen-covering triangle has no back faces worth culling; this keeps the pass
    // independent of the VS winding.
    d.Rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    return d;
}

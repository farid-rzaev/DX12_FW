#pragma once

#include "FullscreenStage.h"

// SSAO pipeline stages. Only non-deducible state is listed here; root signature,
// binding slots and static samplers come from shader reflection.
// To tweak per sample: copy the returned desc and mutate fields before PipelineStage::Create.

inline GraphicsStageDesc MakeSSAOStageDesc(const RenderTarget& output)
{
    return MakeFullscreenStageDesc(L"SSAO", L"SSAO_PS.cso", output);
}

inline GraphicsStageDesc MakeSSAOBlurStageDesc(const RenderTarget& output)
{
    return MakeFullscreenStageDesc(L"SSAO Blur", L"SSAOBlur_PS.cso", output);
}

// Writes AO as grayscale into the HDR target for the AO_Raw / AO_Blurred view modes.
inline GraphicsStageDesc MakeSSAODebugCopyStageDesc(const RenderTarget& output)
{
    return MakeFullscreenStageDesc(L"SSAO Debug Copy", L"SSAODebugCopy_PS.cso", output);
}

#pragma once

#include <Framework/3RD_Party/IMGUI/imgui.h>
#include <Framework/3RD_Party/D3D/d3dx12.h>

#include <Framework/CommandList.h>
#include <Framework/RootSignature.h>
#include <Framework/Material/Texture.h>
#include <Framework/Material/RenderTarget.h>

#include <wrl.h>
#include <memory.h>

class GUI
{
public:
    GUI();
    virtual ~GUI();

    // Initialize the ImGui context.
    virtual bool Initialize();

    // Begin a new frame.
    virtual void NewFrame();
    virtual void Render(std::shared_ptr<CommandList> commandList, const RenderTarget& renderTarget);

    // Destroy the ImGui context.
    virtual void Destroy();

private:
    ImGuiContext*                               m_pImGuiCtx;
    std::unique_ptr<Texture>                    m_FontTexture;
    std::unique_ptr<RootSignature>              m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
};
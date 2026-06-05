#pragma once

// Maximum number of color textures that can be bound to the render target is 8  ==>>  (0 - 7) and one depth-stencil buffer.

#include <DirectXMath.h> // For XMFLOAT2
#include <cstdint>
#include <vector>
#include <memory>

#include "Texture.h"

#include <Framework/3RD_Party/Defines.h>

class Application;

// Don't use scoped enums to avoid the explicit cast required to use these as array indices.
enum AttachmentPoint
{
    Color0,
    Color1,
    Color2,
    Color3,
    Color4,
    Color5,
    Color6,
    Color7,
    DepthStencil,
    NumAttachmentPoints
};

class DX12_FW_API RenderTarget
{
public:
    // Create an empty render target.
    RenderTarget();

    RenderTarget(const RenderTarget& copy)                = default;
    RenderTarget(RenderTarget&& copy)                     = default;
    RenderTarget& operator=(const RenderTarget& other)    = default;
    RenderTarget& operator=(RenderTarget&& other)         = default;

	// ----- OWNERSHIP -----
    // Attach a texture to the render target. The texture will be copied into the texture array.
    // RenderTarget will own attachments.
    void AttachTexture(AttachmentPoint attachmentPoint, const Texture& texture);
    // Attach with Shared ownership. This is required when we have shared buffer (sharedDepthTexture) between different stages (GBuffer and HDR RT)
    void AttachTextureShared(AttachmentPoint attachmentPoint, std::shared_ptr<Texture> texture);

    // ----- OBSERVERS -----
    // Get texture - non-owning observer (borrows texture).
    // The ptr is valid while RenderTarget owns that slot and has not replaced it.
	// The ptr is not expected to be cached by the client, and is not expected to be valid after subsequent calls to AttachTexture or AttachTextureShared.
    const Texture* GetTexture(AttachmentPoint attachmentPoint) const;
    // For internal framework usage, not for clients.
    const Texture* TryGetTexture(AttachmentPoint attachmentPoint) const;

    // Resize all of the textures associated with the render target.
    void Resize(DirectX::XMUINT2 size);
    void Resize(uint32_t width, uint32_t height);
    DirectX::XMUINT2 GetSize() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    // Get a viewport for this render target.
    // The scale and bias parameters can be used to specify a split-screen viewport (the bias parameter is normalized in the range [0...1]).
    // By default, a fullscreen viewport is returned.
    D3D12_VIEWPORT GetViewport(DirectX::XMFLOAT2 scale = { 1.0f, 1.0f }, DirectX::XMFLOAT2 bias = { 0.0f, 0.0f }, float minDepth = 0.0f, float maxDepth = 1.0f) const;

    // Get a list of the textures attached to the render target.
    // This method is primarily used by the CommandList when binding the
    // render target to the output merger stage of the rendering pipeline.
    const std::vector<std::shared_ptr<Texture>>& GetTextures() const;

    // Get the render target formats of the textures currently 
    // attached to this render target object.
    // This is needed to configure the Pipeline state object.
    D3D12_RT_FORMAT_ARRAY GetRenderTargetFormats() const;

    // Get the format of the attached depth/stencil buffer.
    DXGI_FORMAT GetDepthStencilFormat() const;

private:
    
    // RenderTarget owns attachments.
	// This has to be a shared_ptr since the depth buffer can be shared between multiple render targets (GBuffer RT and HDR RT in Sample7).
    std::vector<std::shared_ptr<Texture>> m_Textures = {};
    DirectX::XMUINT2 m_Size;
};
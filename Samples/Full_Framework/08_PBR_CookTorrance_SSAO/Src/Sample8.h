#pragma once

#include <Framework/Material/IndexBuffer.h>
#include <Framework/Material/VertexBuffer.h>
#include <Framework/Material/Texture.h>
#include <Framework/Material/RenderTarget.h>
#include <Framework/Material/Mesh.h>
// --
#include <Framework/Stages/PipelineStage.h>

#include <Framework/Gameplay/AssimpLoader.h>
#include <Framework/Gameplay/Camera.h>
#include <Framework/Gameplay/Light.h>

#include <Framework/Game.h>

#include <DirectXMath.h>

enum class LightingViewMode : uint32_t
{
    Final = 0,           // Normal lit output
    Albedo,              // [RT0] Raw albedo - catches texture/material issues
    Normals,             // [RT1] World-space normals as color - catches normal bugs
    PBR,                 // [RT2] Roughness/Metalness/Emissive
    Depth,               // Linear or Raw depth
    DirectDiffuse,       // Lighting: Direct Diffuse  term only
    DirectSpecular,      // Lighting: Direct Specular term only
    IndirectDiffSpecIBL, // Lighting: IndirectDiffuseIBL + IndirectSpecularIBL from image-based lighting
    IndirectDiffuseIBL,  // Lighting: Indirect Diffuse from image-based lighting
	IndirectSpecularIBL, // Lighting: Indirect Specular from image-based lighting
    AO_Raw,              // AmbientOcclusion: Raw
    AO_Blurred,          // AmbientOcclusion: Blurred
    Count
};

const bool g_CaptureGPUTraceOnLoadAssets = false;

// Must match SSAO_KERNEL_SIZE in SSAO_PS.hlsl (a mismatch fails the cbuffer size check on bind).
const uint32_t SSAO_KERNEL_SIZE = 32;

class Sample8 : public Game
{
public:
    Sample8();
    virtual ~Sample8();

    virtual bool Initialize(const wchar_t* windowTitle, int width, int height, bool vSync = false)  override;

    virtual bool LoadContent() override;
    virtual void UnloadContent() override;

protected:
    virtual void OnUpdate() override;
    virtual void OnRender() override;

    void RenderDeferred(std::shared_ptr<CommandList> commandList, DirectX::CXMMATRIX viewMatrix, DirectX::CXMMATRIX viewProjectionMatrix);

    // Invoked by the registered window when a key is pressed while the window has focus.
    virtual void OnKeyPressed(KeyEventArgs& e) override;

    // Invoked when a key on the keyboard is released.
    virtual void OnKeyReleased(KeyEventArgs& e);

    // Invoked when the mouse is moved over the registered window.
    virtual void OnMouseMoved(MouseMotionEventArgs& e);

    // Invoked when the mouse wheel is scrolled while the registered window has focus.
    virtual void OnMouseWheel(MouseWheelEventArgs& e) override;

    void RescaleRenderTargets(float scale);
    virtual void OnResize(ResizeEventArgs& e) override; 

    void OnGUI();

private: /* IBL - Indirect Diffuse and Specular IBLs */
    Texture m_IrradianceCubemap;
    Texture m_SpecularPrefilterCubemap;
    Texture m_BrdfLut;

private: /* DEFERRED RENDERING */
    
	// Lighting view mode switch - for DEBUG
    LightingViewMode m_LightingViewMode = LightingViewMode::Final;

    static const int NUM_GBUFFER_RTS = 3;   // GBuff RTs: RT0=AlbedoAO, RT1=Normal, RT2=Roughness,Metalness,EmissiveMask
	RenderTarget m_GBufferRT;               // Will hold multiple Color attachment Textures and a Depth buffer for the G-Buffer pass.

    PipelineStage m_GBufferStage;
    PipelineStage m_DeferredLightingStage;

private: /* SSAO */

    RenderTarget m_SSAORawRT;       // R8_UNORM Color0 - SSAO_PS output
    RenderTarget m_SSAOBlurPingRT;  // R8_UNORM Color0 - horizontal blur output
    RenderTarget m_SSAOBlurRT;      // R8_UNORM Color0 - vertical blur output (final AO)

    PipelineStage m_SSAOStage;
    PipelineStage m_SSAOBlurStage;
    PipelineStage m_SSAODebugCopyStage;

    // 4x4 R32G32B32A32_FLOAT, tiled random rotation vectors
    Texture m_SSAONoiseTexture;

    // Tangent-space hemisphere sample offsets, generated once in LoadContent.
    DirectX::XMFLOAT4 m_SSAOKernel[SSAO_KERNEL_SIZE];

    // Runtime tuning (cbuffer values, not PSO state)
    float m_SSAORadius    = 0.5f;
    float m_SSAOBias      = 0.025f;
    float m_SSAOPower     = 2.0f;
    float m_SSAOIntensity = 1.0f;

private:
    // Some geometry to render.
    std::unique_ptr<Mesh> m_SphereMesh;
    std::unique_ptr<Mesh> m_ConeMesh;

    std::unique_ptr<Mesh> m_SkyboxMesh;

    Texture m_DefaultTexture;
    Texture m_GraceCathedralPanoTexture;
    Texture m_GraceCathedralCubemap;

    // HDR Render target
    RenderTarget m_HDRRenderTarget;
    // Same HDR color texture, no depth: full-screen passes that sample G-buffer depth as an SRV
    // can't also have it bound as DSV (SetRenderTarget would transition it back to DEPTH_WRITE).
    RenderTarget m_HDRColorOnlyRT;

    PipelineStage m_SkyboxStage;
    // HDR -> SDR tone mapping into the window's render target.
    PipelineStage m_TonemapStage;

    D3D12_RECT m_ScissorRect;

    Camera m_Camera;
    struct alignas( 16 ) CameraData
    {
        DirectX::XMVECTOR m_InitialCamPos;
        DirectX::XMVECTOR m_InitialCamRot;
        float m_InitialFov;
    };
    CameraData* m_pAlignedCameraData;

    // Camera controller
    float m_Forward;
    float m_Backward;
    float m_Left;
    float m_Right;
    float m_Up;
    float m_Down;

    float m_Pitch;
    float m_Yaw;

    // Rotate the lights in a circle.
    bool m_AnimateLights;
    // Set to true if the Shift key is pressed.
    bool m_Shift;

    int m_Width;
    int m_Height;

    // Scale the HDR render target to a fraction of the window size.
    float m_RenderScale;

    // Define some lights.
    std::vector<PointLight> m_PointLights;
    std::vector<SpotLight> m_SpotLights;

    std::vector<LoadedMeshPart> m_LoadedMeshParts;
};
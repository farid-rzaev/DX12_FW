#pragma once

#include <Framework/Material/IndexBuffer.h>
#include <Framework/Material/VertexBuffer.h>
#include <Framework/Material/Texture.h>
#include <Framework/Material/RenderTarget.h>
#include <Framework/Material/Mesh.h>
// --
#include <Framework/RootSignature.h>

#include <Framework/Gameplay/AssimpLoader.h>
#include <Framework/Gameplay/Camera.h>
#include <Framework/Gameplay/Light.h>

#include <Framework/Game.h>

#include <DirectXMath.h>

enum class RenderingMode
{
    Forward = 0,
    Deferred,
    Count
};

class Sample4 : public Game
{
public:
    Sample4();
    virtual ~Sample4();

    virtual bool Initialize(const wchar_t* windowTitle, int width, int height, bool vSync = false)  override;

    virtual bool LoadContent() override;
    virtual void UnloadContent() override;

protected:
    virtual void OnUpdate() override;
    virtual void OnRender() override;

    void RenderForward(std::shared_ptr<CommandList> commandList, DirectX::CXMMATRIX viewMatrix, DirectX::CXMMATRIX viewProjectionMatrix);
    void RenderDeferred(std::shared_ptr<CommandList> commandList, DirectX::CXMMATRIX viewMatrix, DirectX::CXMMATRIX viewProjectionMatrix);

    // Invoked by the registered window when a key is pressed while the window has focus.
    virtual void OnKeyPressed(KeyEventArgs& e) override;

    // Invoked when a key on the keyboard is released.
    virtual void OnKeyReleased(KeyEventArgs& e);

    // Invoked when the mouse is moved over the registered window.
    virtual void OnMouseMoved(MouseMotionEventArgs& e);

    // Invoked when the mouse wheel is scrolled while the registered window has focus.
    virtual void OnMouseWheel(MouseWheelEventArgs& e) override;

    void RescaleHDRRenderTarget(float scale);
    virtual void OnResize(ResizeEventArgs& e) override; 

    void OnGUI();

private: /* DEFERRED RENDERING */

    // Rendering mode switch
    RenderingMode m_RenderingMode = RenderingMode::Forward;

    // 3 GBuff RTs: Albedo, Normal, Position
    static const int NUM_GBUFFER_RTS = 3;
    RenderTarget m_GBufferRT;  // Will hold multiple color attachments

    // Deferred lighting intermediate buffer (before tone mapping)
    RenderTarget m_DeferredLightingRT;

    // Root signatures for deferred path
    RootSignature m_GBufferRootSignature;
    RootSignature m_DeferredLightingRootSignature;

    // Pipeline state objects for deferred
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_GBufferPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_DeferredLightingPSO;

private:
    // Some geometry to render.
    std::unique_ptr<Mesh> m_SphereMesh;
    std::unique_ptr<Mesh> m_ConeMesh;

    std::unique_ptr<Mesh> m_SkyboxMesh;

    Texture m_DefaultTexture;
    Texture m_DirectXTexture;
    Texture m_GraceCathedralTexture;
    Texture m_GraceCathedralCubemap;

    // HDR Render target
    RenderTarget m_HDRRenderTarget;

    // Root signatures
    RootSignature m_SkyboxSignature;
    RootSignature m_HDRRootSignature;
    RootSignature m_SDRRootSignature;

    // Pipeline state object.
    // Skybox PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SkyboxPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_HDRPipelineState;
    // HDR -> SDR tone mapping PSO.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SDRPipelineState;

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
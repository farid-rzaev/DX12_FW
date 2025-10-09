#pragma once

#include "Camera.h"
#include "Light.h"

#include <Framework/Game.h>

#include <Framework/Material/VertexBuffer.h>
#include <Framework/Material/IndexBuffer.h> 
#include <Framework/Material/Mesh.h>
#include <Framework/Material/Texture.h>
#include <Framework/Material/RenderTarget.h>

#include <Framework/RootSignature.h>

#include <DirectXMath.h>

class Sample1 : public Game
{
public:
    Sample1();
    virtual ~Sample1();

    virtual bool Initialize(const wchar_t* windowTitle, int width, int height, bool vSync = false)  override;

    virtual bool LoadContent() override;
    virtual void UnloadContent() override;

protected:
    // Update the game logic.
    virtual void OnUpdate() override;
    virtual void OnRender() override;
    virtual void OnResize(ResizeEventArgs& e) override; 

    // Invoked by the registered window when a key is pressed while the window has focus.
    virtual void OnKeyPressed(KeyEventArgs& e) override;
    // Invoked when a key on the keyboard is released.
    virtual void OnKeyReleased(KeyEventArgs& e);
    // Invoked when the mouse is moved over the registered window.
    virtual void OnMouseMoved(MouseMotionEventArgs& e);
    // Invoked when the mouse wheel is scrolled while the registered window has focus.
    virtual void OnMouseWheel(MouseWheelEventArgs& e) override;

private:
    // Some geometry to render.
    std::unique_ptr<Mesh> m_CubeMesh   = nullptr;
    std::unique_ptr<Mesh> m_SphereMesh = nullptr;
    std::unique_ptr<Mesh> m_ConeMesh   = nullptr;
    std::unique_ptr<Mesh> m_TorusMesh  = nullptr;
    std::unique_ptr<Mesh> m_PlaneMesh  = nullptr;

    Texture m_DefaultTexture;
    Texture m_DirectXTexture;
    Texture m_EarthTexture;
    Texture m_MonaLisaTexture;

    // Render target
    RenderTarget m_RenderTarget;

    // Root signature
    RootSignature m_RootSignature;

    // Pipeline state object.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;

    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    Camera m_Camera;
    struct alignas( 16 ) CameraData
    {
        DirectX::XMVECTOR m_InitialCamPos;
        DirectX::XMVECTOR m_InitialCamRot;
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

    // Define some lights.
    std::vector<PointLight> m_PointLights;
    std::vector<SpotLight> m_SpotLights;
};
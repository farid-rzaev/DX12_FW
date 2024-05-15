#include "Game.h"

#include <DirectXMath.h>


// =====================================================================================
//								       Init 
// =====================================================================================

Game::Game(HINSTANCE hInstance) : Application(hInstance)
{
}


Game::~Game()
{
}


bool Game::Initialize(const wchar_t* windowTitle, int width, int height, bool vSync)
{
    // Check for DirectX Math library support.
    if (!DirectX::XMVerifyCPUSupport())
    {
        MessageBoxA(NULL, "Failed to verify DirectX Math library support.", "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    Application::Initialize(windowTitle, width, height, vSync);

    return true;
}


// =====================================================================================
//								        Run
// =====================================================================================


void Game::Run()
{
    LoadContent();

    Application::Run();

    UnloadContent();
}


// =====================================================================================
//								      Rendering
// =====================================================================================


void Game::Update()
{
    Application::Update();
}

void Game::Render()
{
    Application::Render();
}

void Game::Resize(UINT32 width, UINT32 height)
{
    Application::Resize(width, height);
}


// =====================================================================================
//							          EVENTS
// =====================================================================================


void Game::OnUpdate(UpdateEventArgs& e)
{
}


void Game::OnRender(RenderEventArgs& e)
{
}


void Game::OnKeyPressed(KeyEventArgs& e)
{
    // By default, do nothing.
}


void Game::OnKeyReleased(KeyEventArgs& e)
{
    // By default, do nothing.
}


void Game::OnMouseMoved(class MouseMotionEventArgs& e)
{
    // By default, do nothing.
}


void Game::OnMouseButtonPressed(MouseButtonEventArgs& e)
{
    // By default, do nothing.
}


void Game::OnMouseButtonReleased(MouseButtonEventArgs& e)
{
    // By default, do nothing.
}
 

void Game::OnMouseWheel(MouseWheelEventArgs& e)
{
    // By default, do nothing.
}


void Game::OnResize(ResizeEventArgs& e)
{
    //m_Width = e.Width;
    //m_Height = e.Height;
}


void Game::OnWindowDestroy()
{
    // If the Window which we are registered to is 
    // destroyed, then any resources which are associated 
    // to the window must be released.
    UnloadContent();
}
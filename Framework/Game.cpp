//#include "Game.h"
//
//#include <Framework/Application.h>
//#include <Framework/Window.h>
//
////#include <External/D3D/d3dx12.h>
//#include <DirectXMath.h>
//
//
//Game::Game(const std::wstring& name, int width, int height, bool vSync)
//    : m_Name(name)
//    , m_Width(width)
//    , m_Height(height)
//    , m_vSync(vSync)
//{
//}
//
//
//Game::~Game()
//{
//    assert(!m_pWindow && "Use Game::Destroy() before destruction.");
//}
//
//
//bool Game::Initialize()
//{
//    // Check for DirectX Math library support.
//    if (!DirectX::XMVerifyCPUSupport())
//    {
//        MessageBoxA(NULL, "Failed to verify DirectX Math library support.", "Error", MB_OK | MB_ICONERROR);
//        return false;
//    }
//
//    m_pWindow = Application::Get().CreateRenderWindow(m_Name, m_Width, m_Height, m_vSync);
//    m_pWindow->RegisterCallbacks(shared_from_this());
//    m_pWindow->Show();
//
//    return true;
//}
//
//
//void Game::Destroy()
//{
//    Application::Get().DestroyWindow(m_pWindow);
//    m_pWindow.reset();
//}
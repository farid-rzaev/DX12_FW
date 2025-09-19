#include "Game.h"

#include <Framework/Application.h>

#include <DirectXMath.h>


// =====================================================================================
//										Init 
// =====================================================================================

Game::Game()
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

	// Only if Window is successfully created - set WndProc and PointerInjection
	if (Application::Get().Initialize(windowTitle, width, height, vSync))
	{
		std::shared_ptr<Window> window = Application::Get().GetWindow();
		if (window)
		{
			// Both of these calls should be called after CreateWindow() call
			window->SetCustomWndProc(Game::WndProc);	// reset the Default WndProc of the window to app's static method
			window->SetUserPtr((void*)this);			// inject (this) pointer to retrive then in WndProc (as WndProc is static and has no access to "this" ptr)
		}
	}

	return true;
}


// =====================================================================================
//										Run
// =====================================================================================


int Game::Run()
{
	LoadContent();

	int retCode = Application::Get().Run();

	UnloadContent();

	return retCode;
}


// =====================================================================================
//									  Rendering
// =====================================================================================


void Game::Update()
{
	Application::Get().Update();
}

void Game::Render()
{
	Application::Get().Render();
}

void Game::Resize(UINT32 width, UINT32 height)
{
	Application::Get().Resize(width, height);
}


// =====================================================================================
//										EVENTS
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

// =====================================================================================
//									WndProc
// =====================================================================================


// The window procedure handles any window messages sent to the application. 
LRESULT CALLBACK Game::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// Retrive from window injected Game the injected pointer
	Game* game = (Game*) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	Application* app = &Application::Get();

	// In order to prevent the application from handling events before the necessary 
	// DirectX 12 objects are created, the m_IsInitialized flag is checked. This flag
	// is set to true in the initialization function after all of the required assets 
	// have been loaded. Trying to resize or render the screen before the swap chain, 
	// command list and command allocators have been created would be disastrous.
	if (game)
	//if (app->m_IsInitialized)
	{
		switch (message)
		{
		case WM_PAINT:
			game->Update();
			game->Render();
			break;
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
		{
			// The WM_SYSKEYDOWN message is sent to the window procedure function when
			//		the Alt key is held while pressing another key combination 
			//		(for example, Alt + Enter).
			// The  WM_KEYDOWN message is sent when any non - system key is pressed
			//		(a key is pressed without Alt being held down).

			bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

			// V					- Toggle V-Sync.
			// Esc					- Exit the application.
			// Alt+Enter, F11		- Toggle fullscreen mode.
			switch (wParam)
			{
			case 'V':
				app->GetWindow()->ToggleVSync();
				break;
			case VK_ESCAPE:
				::PostQuitMessage(0);
				break;
			case VK_RETURN:
				if (alt)
				{
			case VK_F11:
				app->ToggleFullscreen();
				//app->Resize(1, 1);
				}
				break;
			}
		}
		break;
		// ???-TODO-??? Didn't understand why the message is specific for Alt+Enter???
		// The default window procedure will play a system notification sound 
		// when pressing the Alt+Enter keyboard combination if this message is 
		// not handled.
		case WM_SYSCHAR:
			break;
		case WM_SIZE:
		{
			RECT clientRect = {};
			::GetClientRect(app->GetWindow()->GetHWND(), &clientRect);

			int width = clientRect.right - clientRect.left;
			int height = clientRect.bottom - clientRect.top;

			app->Resize(width, height);
		}
		break;
		case WM_DESTROY:
			::PostQuitMessage(0);
			break;
		default:
			return ::DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}
	else
	{
		return ::DefWindowProcW(hwnd, message, wParam, lParam);
	}

	return 0;
}
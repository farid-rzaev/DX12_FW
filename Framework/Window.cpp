#include "Window.h"

#include <cassert>
#include <algorithm> // std::min and  std::max.


Window::Window(UINT32 width, UINT32 height, bool vSync) 
	: m_ClientWidth(width)
	, m_ClientHeight(height)
	, m_VSync(vSync)
{
	m_TearingSupported = CheckTearingSupport();
}


// Before creating an instance of an OS window, the window class corresponding to that window must be registered. 
// The window class will be automatically unregistered when the application terminates.
void Window::RegisterWindowClass(HINSTANCE hInst)
{
	// Register a window class for creating our render window with.
	WNDCLASSEXW windowClass = {};

	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	//windowClass.lpfnWndProc = &WndProc;
	windowClass.lpfnWndProc = DefWindowProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	// hInstance - A handle to the instance that contains the window procedure for the class.
	// This module instance handle is passed to the WinMain function.
	windowClass.hInstance = hInst;
	windowClass.hIcon = ::LoadIcon(hInst, NULL);
	windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	// This member can be a handle to the brush to be used for painting the background, 
	//		or it can be a color value (COLOR_WINDOW, COLOR_BACKGROUND, ...).
	// A color value must be one of the standard system colors (the value 1 must be added to the chosen color)
	// If a color value is given, you must convert it to (HBRUSH)
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = WINDOW_CLASS_NAME;
	windowClass.hIconSm = ::LoadIcon(hInst, NULL);

	static ATOM atom = ::RegisterClassExW(&windowClass);
	assert(atom > 0);
}


HWND Window::CreateWindow(HINSTANCE hInst, const wchar_t* windowTitle)
{
	// The GetSystemMetrics() - retrieves specific system metric information. 
	// the SM_CXSCREEN and SM_CYSCREEN system metric are used to retrieve 
	// the width and height in pixels of the primary display monitor.
	int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

	// To calculate the required size of the window rectangle, 
	//		based on the desired client-rectangle size, the AdjustWindowRect function is used.
	// The WS_OVERLAPPEDWINDOW window style describes a window that can be 
	//		minimized, and maximized, and has a thick window frame.
	RECT windowRect = { 0, 0, static_cast<LONG>(m_ClientWidth), static_cast<LONG>(m_ClientHeight) };
	::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	int windowWidth = windowRect.right - windowRect.left;
	int windowHeight = windowRect.bottom - windowRect.top;

	// Center the window within the screen. Clamp to 0, 0 for the top-left corner.
	int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
	int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

	g_hWnd = ::CreateWindowExW(
		NULL,
		WINDOW_CLASS_NAME,
		windowTitle,
		WS_OVERLAPPEDWINDOW,
		windowX,
		windowY,
		windowWidth,
		windowHeight,
		NULL,
		NULL,
		hInst,
		nullptr
	);

	assert(g_hWnd && "Failed to create window");

	// Query Window rectangle for toggling the full screen state of the window.
	::GetWindowRect(g_hWnd, &g_WindowRect);

	return g_hWnd;
}

// The primary purpose of the swap chain is to present the rendered image to the screen. 
void Window::CreateSwapChain(ComPtr<ID3D12CommandQueue> commandQueue)
{
	ComPtr<IDXGISwapChain4> dxgiSwapChain4;
	ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;
#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	// SwapEffect:
	//
	// To achieve maximum frame rates while rendering with vsync - off[19], the DXGI_SWAP_EFFECT_FLIP_DISCARD
	//		flip model should be used.The discard means that if the previously presented frame is still in 
	//		the queue to be presented, then that frame will be discarded and the next frame will be put 
	//		directly to the front of the presentation queue.
	//
	// When using the DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL 
	//		presentation model, the DXGI runtime will place the presented frame at the end of the presentation
	//		queue.Using this presentation model may cause presentation lag when there are no more buffers to 
	//		utilize as the next back buffer(the IDXGISwapChain1::Present1 method will likely block the calling 
	//		thread until a buffer can be made available).
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = m_ClientWidth;
	swapChainDesc.Height = m_ClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	// Describes multi-sampling parameters:	
	// This member is valid only with bit-block transfer (bitblt) model swap chains. 
	// When using flip model swap chain, this member must be specified as {1, 0}.
	swapChainDesc.SampleDesc = { 1, 0 };  // multi-sampling parameters {Count, Quality};
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	// The number of buffers in the swap chain. When you create a full-screen swap chain, 
	//		you typically include the front buffer in this value. 
	// The minimum number of buffers When using the flip presentation model is two.
	swapChainDesc.BufferCount = NUM_FRAMES_IN_FLIGHT;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	// It is recommended to always allow tearing if tearing support is available.
	swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
		commandQueue.Get(),	// cannot be NULL !!!
		g_hWnd,
		&swapChainDesc,
		nullptr,			// optional param to create a full-screen swap chain. Set it to NULL to create a windowed swap chain.
		nullptr,
		&swapChain1));

	// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
	// will be handled manually.
	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(g_hWnd, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain1.As(&m_SwapChain));

	// To render to the swap chain's back buffers, a render target view (RTV) 
	//		needs to be created for each of the swap chain's back buffers.
}


void Window::ResizeBackBuffers(UINT32 width, UINT32 height)
{
	for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
	{
		// Releasing local references to the swap chain's back buffers
		// as after Resize we'll get the new ones.
		// Any references to the back buffers must be released
		// before the swap chain can be resized.
		m_BackBuffers[i].Reset();
	}

	// Don't allow 0 size swap chain back buffers.
	m_ClientWidth = std::max(1u, width) ;
	m_ClientHeight = std::max(1u, height);

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	ThrowIfFailed(m_SwapChain->GetDesc(&swapChainDesc));
	ThrowIfFailed(m_SwapChain->ResizeBuffers(swapChainDesc.BufferCount, m_ClientWidth, m_ClientHeight,
		swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

}

ComPtr<ID3D12Resource> Window::UpdateBackBufferCache(UINT8 index)
{ 
	ThrowIfFailed(m_SwapChain->GetBuffer(index, IID_PPV_ARGS(&m_BackBuffers[index])));

	return m_BackBuffers[index];
}


UINT8 Window::Present()
{
	// If tearing is supported, it is recommended to always use the 
	// DXGI_PRESENT_ALLOW_TEARING flag when presenting with a sync interval of 0.
	// The requirements for using the DXGI_PRESENT_ALLOW_TEARING flag when 
	// presenting are :
	//
	//		- The swap chain must be created with the DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING flag.
	//		- The sync interval passed into Present(or Present1) must be 0.
	//		- The DXGI_PRESENT_ALLOW_TEARING flag cannot be used in an application that is 
	//			currently in full screen exclusive mode (set by calling SetFullscreenState(TRUE)).
	//			It can only be used in windowed mode.To use this flag in 
	//			full screen Win32 apps, the application should present 
	//			to a fullscreen borderless window and disable automatic Alt + Enter 
	//			fullscreen switching using  IDXGIFactory::MakeWindowAssociation.
	UINT syncInterval = m_VSync ? 1 : 0;
	UINT presentFlags = m_TearingSupported && !m_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	ThrowIfFailed(m_SwapChain->Present(syncInterval, presentFlags));

	// Updating current back buffer index:
	// When using the DXGI_SWAP_EFFECT_FLIP_DISCARD flip model, the order of 
	//     back buffer indicies is not guaranteed to be sequential. 
	//	   The IDXGISwapChain3::GetCurrentBackBufferIndex method is used to 
	//     get the index of the swap chain's current back buffer.
	return m_SwapChain->GetCurrentBackBufferIndex();
}

// =====================================================================================
//							  Pointer Injections
// =====================================================================================

void Window::SetUserPtr(void* userPtr)
{
	// inject Application pointer into window
	SetWindowLongPtr(g_hWnd, GWLP_USERDATA, (LONG_PTR)userPtr);
}

void Window::SetCustomWndProc(WNDPROC wndProc)
{
	// Reset the Default WndProc of the window to app's static method
	SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)wndProc);
}


// =====================================================================================
//								 Helpers
// =====================================================================================

// Variable refresh rate displays (NVidia's G-Sync and AMD's FreeSync) require 
//		tearing to be enabled in the DirectX 12 application to function correctly. 
// This feature is also known as "vsync-off" 
bool Window::CheckTearingSupport()
{
	BOOL allowTearing = FALSE;

	// Rather than create the DXGI 1.5 factory interface directly, we create the
	// DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
	// graphics debugging tools which will not support the 1.5 factory interface 
	// until a future update.
	ComPtr<IDXGIFactory4> factory4;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
	{
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory4.As(&factory5)))
		{
			factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing, sizeof(allowTearing));
		}
	}

	return allowTearing == TRUE;
}


// Since the swap chain's swap effect is using a flip effect, it is NOT necessary 
//		for the window to obtain exclusive ownership of the screen in order to achieve 
//		maximum frame rates.
// Switching the back buffer to a full screen exclusive mode using the  
//		IDXGISwapChain::SetFullscreenState method can be cumbersome and has the following 
//		drawbacks:
//			- A DXGI_SWAP_CHAIN_FULLSCREEN_DESC structure is required when creating 
//			  the swap chain to switch to a full screen state.
//			- The resolution and refresh rate must match one of the supported modes 
//			  of the monitor.Providing incorrect resolution or refresh rate settings 
//			  may cause the screen to go black for the end user.
//			- Switching to full screen exclusive mode might cause any other monitors 
//			  in a multi - monitor setup to turn black.
//			- The mouse cursor is locked to the full screen display.
//			- Switching to a full screen state will fail if the GPU that is rendering 
//			  is is not directly connected to the display device.This is common  
//			  in multi - GPU configurations(for example laptops with an integrated 
//			  Intel graphics chip and a dedicated GPU).
// To address these issues with full screen exclusive mode, the window will be maximized
//		using a full screen borderless window (FSBW) 
// When using a full screen borderless window the window style is changed so that the window 
//		has no decorations (caption, minimize, maximize, close buttons, and frame). The window 
//		is then resized to the full screen dimensions of the nearest display. When using a 
//		multi-monitor setup, it is possible that the end user wants the game window to be on 
//		a different display other than the primary display. To facilitate this functionality, 
//		the window should be made full screen on the display that the application window is 
//		overlapping with the most. The nearest monitor relative to the application window can 
//		be queried using the MonitorFromWindow() function. This function returns a handle to 
//		a monitor which can be used to query the monitor info using the GetMonitorInfo() function.
// The SetFullscreen function is used to switch the window to a full screen borderless window.
void Window::SetFullscreen(bool fullscreen)
{
	if (g_Fullscreen != fullscreen)
	{
		g_Fullscreen = fullscreen;

		if (g_Fullscreen) // Switching to fullscreen.
		{
			// Store the current window dimensions so they can be restored 
			// when switching out of fullscreen state.
			::GetWindowRect(g_hWnd, &g_WindowRect);

			// Set the window style to a borderless window so the client area fills
			// the entire screen.
			UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

			::SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);

			// Query the name of the nearest display device for the window.
			// This is required to set the fullscreen dimensions of the window
			// when using a multi-monitor setup.
			HMONITOR hMonitor = ::MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEX monitorInfo = {};
			monitorInfo.cbSize = sizeof(MONITORINFOEX);
			::GetMonitorInfo(hMonitor, &monitorInfo);

			// The structure returned from the GetMonitorInfo() function contains a rectangle 
			//		structure that describes the full screen rectangle for the monitor.
			// The SetWindowPos function is used to change the position, size and 
			//		z-order (make sure it is above all other visible windows) of the window. 
			// HWND_TOP - Places the window at the top of the Z order.
			::SetWindowPos(g_hWnd, HWND_TOP,
				monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.top,
				monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);
			// SWP_FRAMECHANGED: Applies new frame styles set using the SetWindowLong function. 
			//		Sends a WM_NCCALCSIZE message to the window, even if the window's size is 
			//		not being changed. If this flag is not specified,  WM_NCCALCSIZE is sent 
			//		only when the window's size is being changed.
			// SWP_NOACTIVATE: Does not activate the window.If this flag is not set, the window 
			//		is activated and moved to the top of either the topmost or non - topmost group
			//		(depending on the setting of the hWndInsertAfter parameter).

			// Show the window in a maximized state.
			::ShowWindow(g_hWnd, SW_MAXIMIZE);
		}
		else
		{
			// Restore all the window decorators.
			::SetWindowLong(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

			// HWND_NOTOPMOST - Places the window above all non-topmost windows 
			// (that is, behind all topmost windows). This flag has no effect 
			// if the window is already a non-topmost window.
			::SetWindowPos(g_hWnd, HWND_NOTOPMOST,
				g_WindowRect.left,
				g_WindowRect.top,
				g_WindowRect.right - g_WindowRect.left,
				g_WindowRect.bottom - g_WindowRect.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			// Show the window in a normal state.
			::ShowWindow(g_hWnd, SW_NORMAL);
		}
	}
}
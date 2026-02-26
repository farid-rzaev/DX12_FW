#pragma once

#include <Framework/3RD_Party/Defines.h>

// Minimize the num of
// headers from Windows.h
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// The min/max macros conflict 
// with like-named member functions.
// Only use std::min and std::max 
// defined in <algorithm>.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

// To define a func called CreateWindow,
// the Windows macro needs to be undefined.
#if defined(CreateWindow)
#undef CreateWindow
#endif

// D3D
#include <d3d12.h>
#include <dxgi1_6.h>

// ComPtr
#include <wrl.h>
using Microsoft::WRL::ComPtr;

// Materials
#include <Framework/Material/Texture.h>
#include <Framework/Material/RenderTarget.h>

#include <Framework/GUI.h>

// Window class name. Used for registering / creating the window.
constexpr wchar_t WINDOW_CLASS_NAME[] = L"DX12RenderWindowClass";

constexpr UINT8 NUM_FRAMES_IN_FLIGHT = 3;


class DX12_FW_API Window 
{
public:
	Window(UINT32 width, UINT32 height, bool vSync);
	void InitAndCreate(HINSTANCE hInst, const wchar_t* windowTitle);
	void Show() { ::ShowWindow(g_hWnd, SW_SHOW); }
	virtual ~Window();
	   
	void Update();
	void Resize(UINT32 width, UINT32 height);
	UINT Present(const Texture& texture = Texture());

public:   // SET / GET
	bool IsFullscreen() const			{ return g_Fullscreen; }
	void ToggleFullscreen()				{ SetFullscreen(!g_Fullscreen); }
	void SetFullscreen(bool fullscreen);

	bool IsVSync() const				{ return m_VSync; }
	void ToggleVSync()					{ m_VSync = !m_VSync; }
	void SetVSync(bool vSync)			{ m_VSync = vSync; }

	HWND GetWindowHandle()				{ return g_hWnd; }
	UINT32 GetClientWidth() const		{ return m_ClientWidth; }
	UINT32 GetClientHeight() const		{ return m_ClientHeight; }
	void SetClientWidth(UINT32 width)	{ m_ClientWidth = width; }
	void SetClientHeight(UINT32 height) { m_ClientHeight = height; }

	UINT GetCurrentBackBufferIndex() const { return m_CurrentBackBufferIndex; }
	const RenderTarget& GetRenderTarget() const;

public:    // POINTER INJECTION
	void SetUserPtr(void* userPtr);
	void SetCustomWndProc(WNDPROC wndProc);

protected: // HELPERS
	void RegisterWindowClass(HINSTANCE hInst);
	HWND CreateWindow(HINSTANCE hInst, const wchar_t* windowTitle);
	void CreateSwapChain();
	void UpdateRenderTargetViews();

	// Variable refresh rate displays (NVidia's G-Sync and AMD's FreeSync) require 
	//		tearing to be enabled in the DirectX 12 application to function correctly. 
	// This feature is also known as "vsync-off".
	bool CheckTearingSupport();

private:
	// Windows should not be copied.
	Window(const Window& Window)			= delete;
	Window& operator=(const Window& Window) = delete;

private:
	// Window handle
	HWND     g_hWnd = nullptr;
	UINT32   m_ClientWidth = 1920;
	UINT32   m_ClientHeight = 1080;
	
	UINT64	 m_FenceValues[NUM_FRAMES_IN_FLIGHT];
	uint64_t m_FrameValues[NUM_FRAMES_IN_FLIGHT];
	UINT	 m_CurrentBackBufferIndex;

	// Can be toggled with the Alt+Enter or F11
	bool g_Fullscreen = false;
	// Window rectangle (used to toggle fullscreen state).
	RECT g_WindowRect;

	bool m_TearingSupported = false;
	bool m_VSync = true;

	ComPtr<IDXGISwapChain4> m_SwapChain;

	Texture m_BackBufferTextures[NUM_FRAMES_IN_FLIGHT];
	// Mutable to allow modification in a const function.
	mutable RenderTarget m_RenderTarget;

	GUI m_GUI;
};
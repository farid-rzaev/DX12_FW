#pragma once

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


// Windows Runtime Library. Needed for 
// Microsoft::WRL::ComPtr<> template class.
#include <wrl.h>
using namespace Microsoft::WRL;
// DirectX 12 specific headers.
#include <d3d12.h>
#include <dxgi1_6.h>
// for time chrono::high_resolution_clock,
// uint32_t, uint64_t
#include <chrono>    
// shared_ptr 
#include <memory>

// D3D12 extension library.
#include "d3dx12.h"
// Framework
#include "Helpers.h"
#include "Window.h"
#include "CommandQueue.h"

using Microsoft::WRL::ComPtr;

class Application 
{
public:	
	Application(HINSTANCE hInstance, const wchar_t* windowTitle, int width, int height, bool vSync);
	virtual ~Application();
	
	virtual void Init();
	virtual void Run();

	ComPtr<ID3D12Device2> GetDevice() const { return m_d3d12Device; }
	std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

protected:
	void Update();
	void Render();

	void Resize(uint32_t width, uint32_t height);
	void SetFullscreen(bool fullscreen);
	void ToggleFullscreen();

	void EnableDebugLayer();
	ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
	ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter);
	ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
		D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
	void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
		ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap);
	ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
		D3D12_COMMAND_LIST_TYPE type);
	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
		ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type);

	ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device);
	HANDLE CreateEventHandle();
	uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
		uint64_t& fenceValue);
	void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
		std::chrono::milliseconds duration = std::chrono::milliseconds::max());
	void Flush();

private /*CONSTRUCTORS*/ :
	Application(const Application& app) = delete;
	Application& operator=(const Application& app) = delete;

private /*WINDOW*/ :
	// Window class
	std::shared_ptr<Window> m_Window;
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	// Num of SwapChain BackBuffers.
	// Must not be less than 2 when 
	// using the flip presentation model.
	static const uint8_t m_NumFrames = 3;
	// Depending on the flip model of the swap chain, the index of 
	// the current back buffer in the swap chain may not be sequential
	UINT m_CurrentBackBufferIndex;
	uint32_t m_ClientWidth = 1920;
	uint32_t m_ClientHeight = 1080;

	// Variables to control the swap chain's present method:
	// By default, enable V-Sync.
	// Can be toggled with the V key.
	//   The m_VSync variable controls whether the swap chain's present 
	//   method should wait for the next vertical refresh before presenting
	//   the rendered image to the screen. By default, the swap chain's present 
	//   method will block until the next vertical refresh of the screen.This will 
	//   cap the framerate of the application to the refresh rate of the screen/monitor.
	// 
	//   Setting  m_VSync variable to false will cause the swap chain to present the 
	//   rendered image to the screen as fast as possible which will allow the application 
	//   to render at an unthrottled frame rate but may cause visual artifacts in the 
	//   form of screen tearing.
	bool m_VSync = true;
	bool m_TearingSupported = false;

private /*MAIN*/ :
	// APP instance handle that 
	// this APP was created with.
	HINSTANCE m_hInstance;

	// Heap with RTVs
	ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
	// The size of a descriptor in a descriptor heap is vendor specific 
	//   (Intel, NVidia, and AMD may store descriptors differently). 
	// In order to correctly offset the index into the descriptor heap, 
	//   the size of a single element in the descriptor heap needs 
	//   to be queried during initialization - m_RTVDescriptorSize.
	UINT m_RTVDescriptorSize;

	// DirectX 12 Objects
	ComPtr<ID3D12Device2> m_d3d12Device;
	ComPtr<IDXGISwapChain4> m_SwapChain;  // responsible for presenting the rendered image to the window
	ComPtr<ID3D12Resource> m_BackBuffers[m_NumFrames];

	// Command Queues
	std::shared_ptr<CommandQueue> m_DirectCommandQueue;
	std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
	std::shared_ptr<CommandQueue> m_CopyCommandQueue;

private /*GAME*/:
	uint64_t m_FenceValues[m_NumFrames] = {};

};
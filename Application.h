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
	virtual void Run();
	
	// Getters and Setters
	ComPtr<ID3D12Device2> GetDevice() const { return m_d3d12Device; }
	std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

protected:
	virtual void Render();
	virtual void Resize(uint32_t width, uint32_t height);
	virtual void Update();

	void TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList, ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	void Flush();

	void SetFullscreen(bool fullscreen);
	void ToggleFullscreen();

	void EnableDebugLayer();
	ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
	ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter);

	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
	void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<ID3D12DescriptorHeap> descriptorHeap);

private /*CONSTRUCTORS*/ :
	Application(const Application& app) = delete;
	Application& operator=(const Application& app) = delete;

private /*WINDOW*/ :
	// Making window a member (not inhereting from it)
	// as there could be multiple Windows in future.
	std::shared_ptr<Window> m_Window; 
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);


	static const UINT8 m_NumFrames = 3;
	UINT8 m_CurrentBackBufferIndex;
	ComPtr<ID3D12Resource> m_BackBuffers[m_NumFrames];

private /*MAIN*/ :
	// APP instance handle
	HINSTANCE m_hInstance;

	// Heap with RTVs
	ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
	UINT m_RTVDescriptorSize;

	// DirectX 12 Objects
	ComPtr<ID3D12Device2> m_d3d12Device;

	// Command Queues
	std::shared_ptr<CommandQueue> m_DirectCommandQueue;
	std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
	std::shared_ptr<CommandQueue> m_CopyCommandQueue;

private /*GAME*/:
	uint64_t m_FenceValues[m_NumFrames] = {};
};
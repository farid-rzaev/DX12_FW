#pragma once

// Framework
#include "Window.h"
#include "CommandQueue.h"

// D3D12 extension library.
#include <External/D3D/d3dx12.h>
#include <External/Timer/HighResolutionClock.h>

// ComPtr
#include <wrl.h>
// shared_ptr 
#include <memory>

// DX12 headers.
#include <d3d12.h>
#include <dxgi1_6.h>

using Microsoft::WRL::ComPtr;


class Application 
{
// ------------------------------------------------------------------------------------------
//									Function members
// ------------------------------------------------------------------------------------------
public:
	static void Create(HINSTANCE hInstance, const wchar_t* windowTitle, int width, int height, bool vSync);
	static void Destroy();
	static Application & Get();

	virtual void Run();
	
protected:
	// Init 
	Application(HINSTANCE hInstance);
	void Initialize(const wchar_t* windowTitle, int width, int height, bool vSync);
	virtual ~Application();

	// Deleated
	Application(const Application& app) = delete;
	Application& operator=(const Application& app) = delete;

	// Update & Render & Resize
	virtual void Update();
	virtual void Render();
	virtual void Resize(UINT32 width, UINT32 height);
	UINT8 Present() { return m_Window->Present(); }

	// Fullscreen
	void SetFullscreen(bool fullscreen) { m_Window->SetFullscreen(fullscreen); }
	void ToggleFullscreen() { m_Window->ToggleFullscreen(); }
	
	// Sync frames
	void Flush();

	// Helpers
	void EnableDebugLayer();
	ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
	ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter);
	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT32 numDescriptors);
	void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<ID3D12DescriptorHeap> descriptorHeap);

	// Get and Set
	UINT32 GetClientWidth() const { return m_Window->GetClientWidth(); }
	UINT32 GetClientHeight() const { return m_Window->GetClientHeight(); }
	ComPtr<ID3D12Device2> GetDevice() const { return m_d3d12Device; }
	std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
	UINT GetCurrentBackbufferIndex() const { return m_Window->GetCurrentBackBufferIndex(); }
	ComPtr<ID3D12Resource> GetBackbuffer(UINT BackBufferIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackbufferRTV();
	double GetUpdateTotalTime() { return m_UpdateClock.GetTotalSeconds(); }
	double GetRenderTotalTime() { return m_RenderClock.GetTotalSeconds(); }

// ------------------------------------------------------------------------------------------
//									Data members
// ------------------------------------------------------------------------------------------
private:
	// Window:
	//   Making window a member (not inhereting from it)
	//   as there could be multiple Windows in future.
	std::shared_ptr<Window> m_Window					= nullptr;
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	// APP instance handle
	HINSTANCE m_hInstance;

	// DirectX 12 Objects
	ComPtr<ID3D12Device2> m_d3d12Device;

	// Command Queues
	std::shared_ptr<CommandQueue> m_DirectCommandQueue = nullptr;
	std::shared_ptr<CommandQueue> m_ComputeCommandQueue = nullptr;
	std::shared_ptr<CommandQueue> m_CopyCommandQueue = nullptr;

	// Heap with RTVs
	ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
	UINT m_RTVDescriptorSize;

	// Frametimes
	HighResolutionClock m_UpdateClock;
	HighResolutionClock m_RenderClock;
};
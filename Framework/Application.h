#pragma once

// Framework
#include "Window.h"
#include "CommandQueue.h"
#include "DescriptorAllocation.h"

// D3D12 extension library.
#include <Framework/3RD_Party/D3D/d3dx12.h>
#include <Framework/3RD_Party/Timer/HighResolutionClock.h>

// ComPtr
#include <wrl.h>
// shared_ptr 
#include <memory>

// DX12 headers.
#include <d3d12.h>
#include <dxgi1_6.h>

// Forward Decls
class DescriptorAllocator;

// USINGs
using Microsoft::WRL::ComPtr;

#define USE_DESCRIPTOR_ALLOCAOR

class Application : public std::enable_shared_from_this<Application>
{
// ------------------------------------------------------------------------------------------
//									Function members
// ------------------------------------------------------------------------------------------
public: // STATIC
#if 0 
	static void Create(HINSTANCE hInstance, const wchar_t* windowTitle, int width, int height, bool vSync);
	static void Destroy();
	static Application & Get();
#endif

public:
	// Init 
	Application(HINSTANCE hInstance);
	virtual bool Initialize(const wchar_t* windowTitle, int width, int height, bool vSync = false);
	virtual ~Application();

	// Run
	virtual int Run();

	// STATIC - Get and Set
	static uint64_t GetFrameCount() { return ms_FrameCount; }
	static ComPtr<ID3D12Device2> GetDevice() { return m_d3d12Device; }

public:
	// Update & Render & Resize
	virtual void Update();
	virtual void Render();
	virtual void Resize(UINT32 width, UINT32 height);
	UINT8 Present() { return m_Window->Present(); }

	// Sync frames
	void Flush();

	// Fullscreen
	void SetFullscreen(bool fullscreen) { m_Window->SetFullscreen(fullscreen); }
	void ToggleFullscreen() { m_Window->ToggleFullscreen(); }
	
	// DX12 Helpers
	void EnableDebugLayer();
	ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
	ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter);
	// --
	DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors = 1);
	void ReleaseStaleDescriptors(uint64_t finishedFrame);
	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT32 numDescriptors);
#if defined(USE_DESCRIPTOR_ALLOCAOR)
	void UpdateRenderTargetViews();
#else
	void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<ID3D12DescriptorHeap> descriptorHeap);
#endif

	// Get and Set
	UINT32 GetClientWidth() const { return m_Window->GetClientWidth(); }
	UINT32 GetClientHeight() const { return m_Window->GetClientHeight(); }
	std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
	UINT GetCurrentBackbufferIndex() const { return m_Window->GetCurrentBackBufferIndex(); }
	ComPtr<ID3D12Resource> GetBackbuffer(UINT BackBufferIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackbufferRTV();
	double GetUpdateTotalTime() { return m_UpdateClock.GetTotalSeconds(); }
	double GetRenderTotalTime() { return m_RenderClock.GetTotalSeconds(); }
	std::shared_ptr<Window> GetWindow() { return m_Window; }
	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	// Support checks
	DXGI_SAMPLE_DESC GetMultisampleQualityLevels(DXGI_FORMAT format, UINT numSamples, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE) const;

private:
	// Deleated
	Application(const Application& app)				= delete;
	Application& operator=(const Application& app)	= delete;

// ------------------------------------------------------------------------------------------
//									Data members
// ------------------------------------------------------------------------------------------
private:
	// Window:
	//   Making window a member (not inhereting from it)
	//   as there could be multiple Windows in future.
	std::shared_ptr<Window>				 m_Window				= nullptr;

private:
	// APP instance handle
	HINSTANCE							 m_hInstance;

	// DirectX 12 Objects
	static ComPtr<ID3D12Device2>		 m_d3d12Device;

	// DescriptorAllocators
	std::unique_ptr<DescriptorAllocator> m_DescriptorAllocators[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
	DescriptorAllocation				 m_allocationRTV;

	// Command Queues
	std::shared_ptr<CommandQueue>		 m_DirectCommandQueue	= nullptr;
	std::shared_ptr<CommandQueue>		 m_ComputeCommandQueue	= nullptr;
	std::shared_ptr<CommandQueue>		 m_CopyCommandQueue		= nullptr;
										 
	// Heap with RTVs					 
	ComPtr<ID3D12DescriptorHeap>		 m_RTVDescriptorHeap;
	UINT								 m_RTVDescriptorSize;
										 
	// Frametimes						 
	HighResolutionClock					 m_UpdateClock;
	HighResolutionClock					 m_RenderClock;
										 
	static uint64_t						 ms_FrameCount;
};
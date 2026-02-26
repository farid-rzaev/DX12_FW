#pragma once

#include <Framework/3RD_Party/Defines.h>

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


class DX12_FW_API Application
{
// ------------------------------------------------------------------------------------------
//									Function members
// ------------------------------------------------------------------------------------------
public: // STATIC
	static void Create(HINSTANCE hInstance);
	static void Destroy();
	static Application& Get();

public: // MAIN
	bool Initialize(const wchar_t* windowTitle, int width, int height, bool vSync = false);
	int  Run();

	UINT8 Present(const Texture& texture = Texture()) { return m_Window->Present(texture); }

	// SYNC frames
	void Flush();

public: // DX12 HELPERs
	ComPtr<ID3D12Device2>		  CreateDevice(ComPtr<IDXGIAdapter4> adapter);
	void						  EnableDebugLayer();
	ComPtr<IDXGIAdapter4>		  GetAdapter(bool useWarp);
	
	DescriptorAllocation		  AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors = 1);
	void						  ReleaseStaleDescriptors(uint64_t finishedFrame);
	ComPtr<ID3D12DescriptorHeap>  CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT32 numDescriptors);

	ComPtr<ID3D12Device2>		  GetDevice() { return m_d3d12Device; }
	std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
	UINT						  GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;
	// --
	uint64_t&					  GetFrameCount()	   { return m_FrameCount; }
	
	// SUPPORT checks
	DXGI_SAMPLE_DESC GetMultisampleQualityLevels(DXGI_FORMAT format, UINT numSamples, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE) const;

public: // WINDOW
	bool IsFullscreen()							{ return m_Window->IsFullscreen(); }
	void ToggleFullscreen()						{ m_Window->ToggleFullscreen(); }
	void SetFullscreen(bool fullscreen)			{ m_Window->SetFullscreen(fullscreen); }

	bool IsVSync() const						{ return m_Window->IsVSync(); }
	void ToggleVSync()							{ m_Window->ToggleVSync(); }
	void SetVSync(bool vSync)					{ m_Window->SetVSync(vSync); }

	std::shared_ptr<Window> GetWindow()			{ return m_Window; }

	UINT32 GetClientWidth() const				{ return m_Window->GetClientWidth(); }
	UINT32 GetClientHeight() const				{ return m_Window->GetClientHeight(); }
	UINT   GetCurrentBackbufferIndex() const	{ return m_Window->GetCurrentBackBufferIndex(); }
	const RenderTarget& GetRenderTarget() const	{ return m_Window->GetRenderTarget(); }

private:
	// SINGLETON - private to prevent instantiation
	Application(HINSTANCE hInstance);
	~Application();

	// DELETED - Not supporting copy/assign
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
	ComPtr<ID3D12Device2>				 m_d3d12Device			= nullptr;

	// DescriptorAllocators
	std::unique_ptr<DescriptorAllocator> m_DescriptorAllocators[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
	DescriptorAllocation				 m_allocationRTV;

	// Command Queues
	std::shared_ptr<CommandQueue>		 m_DirectCommandQueue	= nullptr;
	std::shared_ptr<CommandQueue>		 m_ComputeCommandQueue	= nullptr;
	std::shared_ptr<CommandQueue>		 m_CopyCommandQueue		= nullptr;
										 
	// Frametimes						 
	uint64_t							 m_FrameCount			= 0;
};
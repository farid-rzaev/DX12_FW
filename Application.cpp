#include "Application.h"

#include <d3dcompiler.h>
#include <DirectXMath.h>
// STL Headers
#include <algorithm> // std::min and  std::max.
// Assert
#include <cassert>


Application::Application(HINSTANCE hInstance, const wchar_t* windowTitle, int width, int height, bool vSync) :
	m_hInstance (hInstance),
	m_ClientWidth(width),
	m_ClientHeight(height),
	m_VSync(vSync)
{
	// Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
	// The SetThreadDpiAwarenessContext function sets the DPI awareness for the 
	// current thread. The DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is an improved 
	// per - monitor DPI awarenes mode which provides new DPI - related scaling behaviours
	// on a per top - level window basis[35]. Using this DPI awareness mode, the 
	// application is able to achieve 100% pixel scaling for the client area of 
	// the window while still allowing for DPI scaling for non - client areas 
	// (such as the title bar and menus). This means that the swap chain buffers 
	// will be resized to fill the total number of screen pixels (true 4K or 8K resolutions)
	// when resizing the client area of the window instead of scaling the client 
	// area based on the DPI scaling settings.
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	EnableDebugLayer();
	
	// Window
	{
		m_Window = std::make_shared<Window>();
		m_TearingSupported = m_Window->CheckTearingSupport();
		m_Window->RegisterWindowClass(m_hInstance);
		m_Window->CreateWindow(m_hInstance, windowTitle, m_ClientWidth, m_ClientHeight);
		// Pointer ijections for WndProc
		m_Window->SetUserPtr((void*)this);					// - inject Application pointer into window
		m_Window->SetCustomWndProc(Application::WndProc);   // - reset the Default WndProc of the 
															//   window to app's static method
	}

	// DirectX 12 objects
	{	
		ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(false);

		if (dxgiAdapter4)
			m_d3d12Device = CreateDevice(dxgiAdapter4);

		if (m_d3d12Device)
			m_DirectCommandQueue  = std::make_shared<CommandQueue> (m_d3d12Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
			m_ComputeCommandQueue = std::make_shared<CommandQueue> (m_d3d12Device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
			m_CopyCommandQueue    = std::make_shared<CommandQueue> (m_d3d12Device, D3D12_COMMAND_LIST_TYPE_COPY);

		if (m_DirectCommandQueue && m_Window)
			m_SwapChain = m_Window->CreateSwapChain(m_DirectCommandQueue->GetD3D12CommandQueue(), m_ClientWidth, m_ClientHeight, m_NumFrames);
	}
}

Application::~Application() {
	// Make sure the command queue has finished all commands before closing.
	//
	// It is important to make sure that any resources that may currently 
	//		be "in-flight" on the GPU have finished processing before they are released.
	// Since all DirectX 12 objects are held by ComPtr's, they will automatically 
	//		be cleaned up when the application exits but this cleanup should not 
	//		occur until the GPU is using them
	Flush();
}

void Application::Init() 
{
	// The first back buffer index will very likely be 0, but it depends
	m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

	m_RTVDescriptorHeap = CreateDescriptorHeap(m_d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, m_NumFrames);
	m_RTVDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	// Render target views are fill into the descriptor heap
	UpdateRenderTargetViews(m_d3d12Device, m_SwapChain, m_RTVDescriptorHeap);

	// Show Window
	m_Window->Show();
}

void Application::Run() {
	// Messages are dispatched to the window procedure (the WndProc function)
	// until the WM_QUIT message is posted to the message queue using the 
	// PostQuitMessage function (this happens in the WndProc function).
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
}

std::shared_ptr<CommandQueue> Application::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const 
{
	std::shared_ptr<CommandQueue> commandQueue;
	switch (type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		commandQueue = m_DirectCommandQueue;
		break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		commandQueue = m_ComputeCommandQueue;
		break;
	case D3D12_COMMAND_LIST_TYPE_COPY:
		commandQueue = m_CopyCommandQueue;
		break;
	default:
		assert(false && "Invalid command queue type.");
	}

	return commandQueue;
}

// For this lesson, the functuion ony display's the frame-rate each second in the debug output 
//		in Visual Studio.
void Application::Update()
{
	static uint64_t frameCounter = 0;
	static double elapsedSeconds = 0.0;
	static std::chrono::high_resolution_clock clock;
	static auto t0 = clock.now();

	frameCounter++;
	auto t1 = clock.now();
	auto deltaTime = t1 - t0;
	t0 = t1;

	// The deltaTime time_point variable stores the number of NANOseconds
	// since the previous call to the Update function. To convert the 
	// deltaTime from nanoseconds into seconds, it is multiplied by 10^(9).
	elapsedSeconds += deltaTime.count() * 1e-9;

	// The frame-rate is printed to the debug output in Visual Studio 
	// only once per second.
	if (elapsedSeconds > 1.0)
	{
		wchar_t buffer[500];
		auto fps = frameCounter / elapsedSeconds;
		swprintf(buffer, 500, L"FPS: %f\n", fps);
		OutputDebugString(buffer);

		frameCounter = 0;
		elapsedSeconds = 0.0;
	}
}

// Resources must be transitioned from one state to another using a resource BARRIER
//		and inserting that resource barrier into the command list.
// For example, before you can use the swap chain's back buffer as a render target, 
//		it must be transitioned to the RENDER_TARGET state and before it can be used
//		for presenting the rendered image to the screen, it must be transitioned to 
//		the  PRESENT state.
// 
//
// There are several types of resource barriers :
//	 - Transition: Transitions a(sub)resource to a particular state before using it. 
//			For example, before a texture can be used in a pixel shader, it must be 
//			transitioned to the PIXEL_SHADER_RESOURCE state.
//	 - Aliasing: Specifies that a resource is used in a placed or reserved heap when
//			that resource is aliased with another resource in the same heap.
//	 - UAV: Indicates that all UAV accesses to a particular resource have completed 
//			before any future UAV access can begin.This is necessary when the UAV is 
//			transitioned for :
//				* Read > Write: Guarantees that all previous read operations on the UAV
//						have completed before being written to in another shader.
//				* Write > Read: Guarantees that all previous write operations on the UAV
//						have completed before being read from in another shader.
//				* Write > Write: Avoids race conditions that could be caused by different
//						shaders in a different draw or dispatch trying to write to the same
//						resource(does not avoid race conditions that could be caused in the
//						same draw or dispatch call).
//				* A UAV barrier is not needed if the resource is being used as a 
//						read - only(Read > Read) resource between draw or dispatches.
void Application::Render()
{
	auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto commandList = m_DirectCommandQueue->GetCommandList();
	auto backBuffer = m_BackBuffers[m_CurrentBackBufferIndex];

	// Clear the render target:
	//		Before the render target can be cleared, it must be transitioned
	//		to the RENDER_TARGET state.
	// 
	//		The resource transition must specify both the before and after 
	//		statesof the(sub)resource.This implies that the before state of
	//		the resource must be known.The state of the resource cannot be 
	//		queried from the resource itself which implies that the 
	//		application developer must track the last know state of 
	//		the resource.
	// 
	//		If there is more than a single resource barrier to insert 
	//		into the command list, it is recommended to store all barriers
	//		in a list and execute them all at the same time before an 
	//		operation that requires the resource to be in a particular 
	//		state is executed.
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			backBuffer.Get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
		);
		commandList->ResourceBarrier(1, &barrier);

		FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
			m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			m_CurrentBackBufferIndex, m_RTVDescriptorSize
		);
		commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	}

	// PRESENT image to the screen
	{
		// After rendering the scene, the current back buffer is PRESENTed 
		//     to the screen.
		// !!! Before presenting, the back buffer resource must be 
		//     transitioned to the PRESENT state.
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			backBuffer.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		commandList->ResourceBarrier(1, &barrier);

		// Execute
		m_FenceValues[m_CurrentBackBufferIndex] = commandQueue->ExecuteCommandList(commandList);


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
		m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

		commandQueue->WaitForFanceValue(m_FenceValues[m_CurrentBackBufferIndex]);
	}
}



// A resize event is triggered when the window is created the first time. 
// It is also triggered when switching to full-screen mode or if the user 
// resizes the window by dragging the window border frame while in windowed mode.
// The Resize function will resize the swap chain buffers if the client area of 
// the window changes.
void Application::Resize(uint32_t width, uint32_t height)
{
	if (m_ClientWidth != width || m_ClientHeight != height)
	{
		// Don't allow 0 size swap chain back buffers.
		m_ClientWidth = std::max(1u, width);
		m_ClientHeight = std::max(1u, height);

		// Flush the GPU Command Queue to make sure the swap chain's back buffers
		// are not being referenced by an in-flight command list.
		Flush();

		for (int i = 0; i < m_NumFrames; ++i)
		{
			// Releasing local references to the swap chain's back buffers
			// as after Resize we'll get the new ones.
			// Any references to the back buffers must be released
			// before the swap chain can be resized.
			m_BackBuffers[i].Reset();
		}

		DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
		ThrowIfFailed(m_SwapChain->GetDesc(&swapChainDesc));
		ThrowIfFailed(m_SwapChain->ResizeBuffers(m_NumFrames, m_ClientWidth, m_ClientHeight,
			swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

		// Since the index of back buffer may not be the same, it is important
		// to update the current back buffer index as known by the application.
		m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

		// After the swap chain buffers have been resized, the descriptors 
		// that refer to those buffers needs to be updated. 
		UpdateRenderTargetViews(m_d3d12Device, m_SwapChain, m_RTVDescriptorHeap);
	}
}

void Application::SetFullscreen(bool fullscreen) {
	m_Window->SetFullscreen(fullscreen);
}
void Application::ToggleFullscreen() {
	m_Window->ToggleFullscreen();
}



void Application::EnableDebugLayer()
{
	// Attempting to enable the debug layer after the Direct3D 12 device context
	// has been created will cause the device to be released.
#if defined(_DEBUG)
	// Always enable the debug layer before doing anything DX12 related
	// so all possible errors generated while creating DX12 objects
	// are caught by the debug layer.
	ComPtr<ID3D12Debug> debugInterface;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
	debugInterface->EnableDebugLayer();

	// The IID_PPV_ARGS macro is used to retrieve an interface pointer, 
	// supplying the IID value of the requested interface automatically
	// based on the type of the interface pointer used.
#endif
}



ComPtr<IDXGIAdapter4> Application::GetAdapter(bool useWarp)
{
	ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;
#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	// Enabling the DXGI_CREATE_FACTORY_DEBUG flag during factory creation enables 
	//		errors to be caught during device creation and while querying for the adapters.
	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> dxgiAdapter1;
	ComPtr<IDXGIAdapter4> dxgiAdapter4;

	if (useWarp)
	{
		// The IDXGIFactory4::EnumWarpAdapter method takes a pointer to a IDXGIAdapter1 interface 
		//		but the GetAdapter function returns a pointer to a IDXGIAdapter4 interface. 
		// In order to cast a COM object to the correct type, the ComPtr::As method should be used.
		ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
		ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
	}
	else
	{
		SIZE_T maxDedicatedVideoMemory = 0;
		for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
			dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

			// Check to see if the adapter can create a D3D12 device without actually 
			//		creating it. The adapter with the largest dedicated video memory
			//		is favored.
			// To verify that the adapter returned from the IDXGIFactory1::EnumAdapters1 
			//		method is a compatible DirectX 12 adapter, a (null) device is created
			//		using the D3D12CreateDevice.
			// D3D_FEATURE_LEVEL  - MinimumFeatureLevel: The minimum D3D_FEATURE_LEVEL 
			//		required for successful device creation.
			if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
				SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
					D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
				dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
			{
				maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
				ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
			}
		}
	}

	return dxgiAdapter4;
}

// The DirectX 12 device is used to create resources (such as textures and buffers, command lists,
//		command queues, fences, heaps, etc...). 
// The DirectX 12 device is not directly used for issuing draw or dispatch commands. 
// The DirectX 12 device can be considered a memory context that tracks allocations in GPU memory.
// Destroying the DirectX 12 device will cause all of the resources allocated by the device to become invalid.
// If the device is destroyed before all of the resources that were created by the device, then the 
//		debug layer will issue warnings about those objects that are still being referenced.
ComPtr<ID3D12Device2> Application::CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
	// D3D_FEATURE_LEVEL  - MinimumFeatureLevel: The minimum D3D_FEATURE_LEVEL 
	//		required for successful device creation.
	ComPtr<ID3D12Device2> d3d12Device2;
	ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));


	// 1) Enable debug messages in debug mode.
	// 2) ID3D12InfoQueue interface is used to enable break points based on the severity
	//		of the message and the ability to filter certain messages from being generated.
	// 3) SetBreakOnSeverity method sets a message severity level to break on (while the 
	//		application is attached to a debugger) when a message with that severity level 
	//		passes through the storage filter.
	// 4) It may not be practical (or feasible) to address all of the possible warnings that can occur.
	//	  In such a case, some warning messages can be ignored. A D3D12_INFO_QUEUE_FILTER can be specified 
	//		to ignore certain warning messages that are generated by the debug layer.
#if defined(_DEBUG)
	ComPtr<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
	{
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		// Suppress whole categories of messages
		//D3D12_MESSAGE_CATEGORY Categories[] = {};

		// Suppress messages based on their severity level
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};

		// Suppress individual messages by their ID
		D3D12_MESSAGE_ID DenyIds[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
		};

		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		//NewFilter.DenyList.NumCategories = _countof(Categories);
		//NewFilter.DenyList.pCategoryList = Categories;
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;

		ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
	}
#endif

	return d3d12Device2;
}

// Before creating the swap chain, the command queue must be created first.
ComPtr<ID3D12CommandQueue> Application::CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

	return d3d12CommandQueue;
}

// A descriptor heap can be considered an array of resource VIEWs.
//
// CBV, SRV, and UAV can be stored in the same heap but
// RTV and Sampler views EACH require separate descriptor heaps.
ComPtr<ID3D12DescriptorHeap> Application::CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
	D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;

	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

	return descriptorHeap;
}

// A render target view (RTV) describes a resource that can be attached to a 
//		bind slot of the output merger stage
void Application::UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
	ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
	// The size of a single descriptor in a descriptor heap is vendor specific and is queried 
	//		using the ID3D12Device::GetDescriptorHandleIncrementSize()
	auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < m_NumFrames; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		// nullptr - description is used to create a default descriptor for the resource
		device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

		m_BackBuffers[i] = backBuffer;

		rtvHandle.Offset(rtvDescriptorSize);
	}
}

// Command allocators and command lists are required to issue rendering commands to the GPU
//		 A command allocator is the backing memory used by a command list.
//		 A command allocator can only be used by a single command list at a time but 
//		 can be reused after the commands that were recorded into the command list 
//		 have finished executing on the GPU (a GPU fence is used to check).
// The memory allocated by the command allocator is reclaimed using the  
//		ID3D12CommandAllocator::Reset method. A command allocator can only be reset after 
//		the commands recorded in the command list have finished executing on the GPU
//		(a GPU fence is used to check).
// In order to achieve maximum frame-rates for the application, one command allocator per
//		"in-flight" command list should be created.
ComPtr<ID3D12CommandAllocator> Application::CreateCommandAllocator(ComPtr<ID3D12Device2> device,
	D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

	return commandAllocator;
}

// Note, the command list can be reused immediately after it has been executed on the command queue.
//		the command list must be reset first before recording any new commands.
ComPtr<ID3D12GraphicsCommandList> Application::CreateCommandList(ComPtr<ID3D12Device2> device,
	ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

	ThrowIfFailed(commandList->Close());

	return commandList;
}



// The table summarizes the methods to use to synchronize with a FENCE object:
//
//					CPU															GPU
//		Signal		ID3D12Fence::Signal											ID3D12CommandQueue::Signal
//		Wait		ID3D12Fence::SetEventOnCompletion, WaitForSingleObject		ID3D12CommandQueue::Wait
// 
// Note:
//		Each thread or GPU queue should have at least one fence object and a corresponding fence value.
//		!!! The same fence object should not be signaled from more than one thread or GPU queue  !!!
//				but more than one thread or queue can wait on the same fence to be signaled.
ComPtr<ID3D12Fence> Application::CreateFence(ComPtr<ID3D12Device2> device)
{
	ComPtr<ID3D12Fence> fence;

	ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

	return fence;
}

// If the fence has not yet been signaled with specific value, the CPU thread will need to block any 
//		further processing until the fence has been signaled with that value. 
HANDLE Application::CreateEventHandle()
{
	HANDLE fenceEvent;

	fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent && "Failed to create fence event.");

	return fenceEvent;
}

// Signal() is used to signal the fence from the GPU. Should be noted that when using the ID3D12CommandQueue::Signal
//		method to signal a fence from the GPU, the fence is not signaled immediatly but is only signaled once the GPU
//		command queue has reached that point during execution. Any commands that have been queued before the signal 
//		method was invoked must complete execution before the fence will be signaled.
// Signal() returns the fence value that the CPU thread should wait for before reusing any resources that are "in-flight"
//		for that frame on the GPU.
uint64_t Application::Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
	uint64_t& fenceValue)
{
	uint64_t fenceValueForSignal = ++fenceValue;
	ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));

	return fenceValueForSignal;
}

// It is possible that the CPU thread will need to stall to wait for the GPU queue to finish
// 		executing commands that write to resources before being reused.
// For example, before reusing a swap chain's back buffer resource, any commands that are 
//		using that resource as a render target must be complete before that back buffer 
//		resource can be reused.
// Any resources that are never used as a writeable target (for example material textures) 
// 		do not need to be double buffered and do not require stalling the CPU thread before 
//		being reused as read-only resources in a shader. 
void Application::WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
	std::chrono::milliseconds duration)
{
	UINT64 ___fenceCompVal_debug = fence->GetCompletedValue();
	if (fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
	}
}

// It is sometimes useful to wait until all previously executed commands have finished
//		executing before doing something (for example, resizing the swap chain buffers 
//		requires any references to the buffers to be released). For this, the Flush 
//		function is used to ensure the GPU has finished processing all commands before
//		continuing.
// The Flush function is used to ensure that any commands previously executed on the GPU
//		have finished executing before the CPU thread is allowed to continue processing. 
// This is useful for ensuring that any back buffer resources being referenced by a command 
//		that is currently "in-flight" on the GPU have finished executing before being resized.
// It is also strongly advisable to flush the GPU command queue before releasing any resources
//		that might be referenced by a command list that is currently "in-flight" on the command 
//		queue (for example, before closing the application). The Flush function will block the 
//		calling thread until the fence value has been reached. After this function returns, it is
//		safe to release any resources that were referenced by the GPU.
// The Flush function is simply a Signal followed by a WaitForFenceValue.)))
void Application::Flush()
{
	m_DirectCommandQueue->Flush();
	m_ComputeCommandQueue->Flush();
	m_CopyCommandQueue->Flush();
}



// The window procedure handles any window messages sent to the application. 
LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Application* app = (Application*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

	// In order to prevent the application from handling events before the necessary 
	// DirectX 12 objects are created, the m_IsInitialized flag is checked. This flag
	// is set to true in the initialization function after all of the required assets 
	// have been loaded. Trying to resize or render the screen before the swap chain, 
	// command list and command allocators have been created would be disastrous.
	if (app)
	//if (app->m_IsInitialized)
	{
		switch (message)
		{
		case WM_PAINT:
			app->Update();
			app->Render();
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
				app->m_VSync = !app->m_VSync;
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
			::GetClientRect(app->m_Window->GetHWND(), &clientRect);

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
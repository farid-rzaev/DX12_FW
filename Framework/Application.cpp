#include "Application.h"
// --
#include <d3dcompiler.h>
#include <DirectXMath.h>
// STL Headers
#include <algorithm> // std::min and  std::max.
// Assert
#include <cassert>


// =====================================================================================
//										Init 
// =====================================================================================

Application::Application(HINSTANCE hInstance, const wchar_t* windowTitle, int width, int height, bool vSync) :
	m_hInstance (hInstance)
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
	
	// DirectX 12 objects
	{	
		ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(false);

		if (dxgiAdapter4)
			m_d3d12Device = CreateDevice(dxgiAdapter4);

		if (m_d3d12Device) 
		{
			m_DirectCommandQueue  = std::make_shared<CommandQueue> (m_d3d12Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
			m_ComputeCommandQueue = std::make_shared<CommandQueue> (m_d3d12Device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
			m_CopyCommandQueue    = std::make_shared<CommandQueue> (m_d3d12Device, D3D12_COMMAND_LIST_TYPE_COPY);
		}
	}

	// Window
	if (m_DirectCommandQueue)
	{
		m_Window = std::make_shared<Window>(width, height, vSync);
		m_Window->RegisterWindowClass(m_hInstance);
		m_Window->CreateWindow(m_hInstance, windowTitle);
		m_Window->CreateSwapChain(m_DirectCommandQueue->GetD3D12CommandQueue());

		// Pointer ijections for WndProc
		m_Window->SetUserPtr((void*)this);					// - inject Application pointer into window
		m_Window->SetCustomWndProc(Application::WndProc);   // - reset the Default WndProc of the 
															//   window to app's static method
	}

	//  Create RTVs in DescriptorHeap
	{
		m_RTVDescriptorHeap = CreateDescriptorHeap(m_d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NUM_FRAMES_IN_FLIGHT);
		// The size of a descriptor in a descriptor heap is vendor specific 
		//   (Intel, NVidia, and AMD may store descriptors differently). 
		// In order to correctly offset the index into the descriptor heap, 
		//   the size of a single element in the descriptor heap needs 
		//   to be queried during initialization - m_RTVDescriptorSize.
		m_RTVDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Render target views are fill into the descriptor heap
		UpdateRenderTargetViews(m_d3d12Device, m_RTVDescriptorHeap);
	}

	// Show Window
	m_Window->Show();
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

// =====================================================================================
//										Run 
// =====================================================================================

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

	// Flush any commands in the 
	// commands queues before quiting.
	Flush();
}

// =====================================================================================
//									Get and Set
// =====================================================================================

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

ComPtr<ID3D12Resource> Application::GetBackbuffer(UINT BackBufferIndex)
{
	return m_Window->GetBackBuffer(BackBufferIndex);
}

D3D12_CPU_DESCRIPTOR_HANDLE Application::GetCurrentBackbufferRTV()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		GetCurrentBackbufferIndex(), m_RTVDescriptorSize
	);

	return rtv;
}

// =====================================================================================
//							  Update & Render & Resize
// =====================================================================================

// For this lesson, the functuion ony display's the frame-rate each second in the debug output 
//		in Visual Studio.
void Application::Update()
{
	// Timer
	m_UpdateClock.Tick();

	static uint64_t frameCount = 0;
	static double totalTime = 0.0;

	totalTime += m_UpdateClock.GetDeltaSeconds();
	frameCount++;

	// The frame-rate is printed to the debug output only once per second.
	if (totalTime > 1.0)
	{
		wchar_t buffer[500];
		auto fps = frameCount / totalTime;
		swprintf(buffer, 500, L"FPS: %f\n", fps);
		OutputDebugString(buffer);

		frameCount = 0;
		totalTime = 0.0;
	}
}

void Application::Render()
{
	// Timer
	m_RenderClock.Tick();
}


// A resize event is triggered when the window is created the first time. 
// It is also triggered when switching to full-screen mode or if the user 
// resizes the window by dragging the window border frame while in windowed mode.
// The Resize function will resize the swap chain buffers if the client area of 
// the window changes.
void Application::Resize(UINT32 width, UINT32 height)
{
	if (m_Window->GetClientWidth() != width || m_Window->GetClientHeight() != height)
	{
		// Flush the GPU Command Queue to make sure the swap chain's back buffers
		// are not being referenced by an in-flight command list.
		Flush();

		m_Window->ResizeBackBuffers(width, height);

		// After the swap chain buffers have been resized, the descriptors 
		// that refer to those buffers needs to be updated. 
		UpdateRenderTargetViews(m_d3d12Device, m_RTVDescriptorHeap);
	}
}

// A render target view (RTV) describes a resource that can be attached to a 
//		bind slot of the output merger stage
void Application::UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
	// The size of a single descriptor in a descriptor heap is vendor specific and is queried 
	//		using the ID3D12Device::GetDescriptorHandleIncrementSize()
	auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer = m_Window->UpdateBackBufferCache(i);
		// nullptr - description is used to create a default descriptor for the resource
		device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

		rtvHandle.Offset(rtvDescriptorSize);
	}
}

// =====================================================================================
//									   Sync
// =====================================================================================

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

// =====================================================================================
//									Helper Funcs
// =====================================================================================

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

// A descriptor heap can be considered an array of resource VIEWs.
//
// CBV, SRV, and UAV can be stored in the same heap but
// RTV and Sampler views EACH require separate descriptor heaps.
ComPtr<ID3D12DescriptorHeap> Application::CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
	D3D12_DESCRIPTOR_HEAP_TYPE type, UINT32 numDescriptors)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;

	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

	return descriptorHeap;
}


// =====================================================================================
//										WndProc
// =====================================================================================

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
				app->m_Window->ToggleVSync();
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
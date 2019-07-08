#include "Game.h"

Game::Game(HINSTANCE hInstance, const wchar_t * windowTitle, int width, int height, bool vSync) :
	Application(hInstance, windowTitle, width, height, vSync),
	m_ScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX)),
	m_Viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height)),
	m_FoV(45.0f)
{
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

	// The first back buffer index will very likely be 0, but it depends
	m_CurrentBackBufferIndex = Application::GetCurrentBackbufferIndex();

}
Game::~Game() 
{

}


void Game::Update() 
{ 
	Application::Update(); 
}


// A resize event is triggered when the window is created the first time. 
// It is also triggered when switching to full-screen mode or if the user 
// resizes the window by dragging the window border frame while in windowed mode.
// The Resize function will resize the swap chain buffers if the client area of 
// the window changes.
void Game::Resize(UINT32 width, UINT32 height) 
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

		// Since the index of back buffer may not be the same, it is important
		// to update the current back buffer index as known by the application.
		m_CurrentBackBufferIndex = Application::GetCurrentBackbufferIndex();
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
void Game::Render() 
{
	auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto commandList = commandQueue->GetCommandList();
	m_CurrentBackBufferIndex = GetCurrentBackbufferIndex();
	auto backBuffer = Application::GetBackbuffer(m_CurrentBackBufferIndex);

	{
		TransitionResource(commandList, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

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
		TransitionResource(commandList, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		// Execute
		m_FenceValues[m_CurrentBackBufferIndex] = commandQueue->ExecuteCommandList(commandList);

		m_CurrentBackBufferIndex = m_Window->Present();
		commandQueue->WaitForFanceValue(m_FenceValues[m_CurrentBackBufferIndex]);
	}
}

// A render target view (RTV) describes a resource that can be attached to a 
//		bind slot of the output merger stage
void Game::UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
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

void Game::TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList, ComPtr<ID3D12Resource> resource, 
	D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition
	(
		resource.Get(), before, after
	);

	commandList->ResourceBarrier(1, &barrier);
}

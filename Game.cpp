#include "Game.h"

// =====================================================================================
//										Init 
// =====================================================================================
Game::Game(HINSTANCE hInstance, const wchar_t * windowTitle, int width, int height, bool vSync) :
	Application(hInstance, windowTitle, width, height, vSync),
	m_ScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX)),
	m_Viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height)),
	m_FoV(45.0f)
{
	// The first back buffer index will very likely be 0, but it depends
	m_CurrentBackBufferIndex = Application::GetCurrentBackbufferIndex();
}
Game::~Game() 
{

}

// =====================================================================================
//							  Update & Render & Resize
// =====================================================================================
void Game::Update() 
{ 
	Application::Update(); 
}

void Game::Resize(UINT32 width, UINT32 height) 
{
	Application::Resize(width, height);

	// Since the index of back buffer may not be the same, it is important
	// to update the current back buffer index as known by the application.
	m_CurrentBackBufferIndex = Application::GetCurrentBackbufferIndex();
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
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv = GetCurrentBackbufferRTV();
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

		m_CurrentBackBufferIndex = Application::Present();
		commandQueue->WaitForFanceValue(m_FenceValues[m_CurrentBackBufferIndex]);
	}
}

// =====================================================================================
//									Helper Funcs
// =====================================================================================
void Game::TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList, ComPtr<ID3D12Resource> resource, 
	D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition
	(
		resource.Get(), before, after
	);

	commandList->ResourceBarrier(1, &barrier);
}

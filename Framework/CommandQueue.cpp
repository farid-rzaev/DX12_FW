#include <cassert>
#include "../Helpers/Helpers.h"

#include "CommandQueue.h"



CommandQueue::CommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type) 
	: m_d3d12Device(device)
	, m_CommandListType(type)
	, m_FenceValue(0)
{
	D3D12_COMMAND_QUEUE_DESC desc;
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	ThrowIfFailed(m_d3d12Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_d3d12CommandQueue)));
	ThrowIfFailed(m_d3d12Device->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12Fence)));

	m_FenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(m_FenceEvent && "Failed to create fence event handle.");
}

CommandQueue::~CommandQueue()
{
	// Releasing the handle to the fence event object.
	::CloseHandle(m_FenceEvent);
}


UINT64 CommandQueue::Signal() {
	UINT64 fenceValue = ++m_FenceValue;
	m_d3d12CommandQueue->Signal(m_d3d12Fence.Get(), fenceValue);
	return fenceValue;
}


bool CommandQueue::IsFenceComplete(UINT64 fenceValue)
{
	return m_d3d12Fence->GetCompletedValue() >= fenceValue;
}


void CommandQueue::WaitForFenceValue(UINT64 fenceValue) 
{
	if (!IsFenceComplete(fenceValue)) 
	{
		m_d3d12Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
		::WaitForSingleObject(m_FenceEvent, DWORD_MAX);
	}
}


void CommandQueue::Flush()
{
	WaitForFenceValue(Signal());
}


ComPtr<ID3D12CommandAllocator> CommandQueue::CreateCommandAllocator()
{
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ThrowIfFailed(
		m_d3d12Device->CreateCommandAllocator(m_CommandListType, IID_PPV_ARGS(&commandAllocator))
	);

	return commandAllocator;
}


ComPtr<ID3D12GraphicsCommandList2> CommandQueue::CreateCommandList(ComPtr<ID3D12CommandAllocator> allocator)
{
	ComPtr<ID3D12GraphicsCommandList2> commandList;
	ThrowIfFailed(
		m_d3d12Device->CreateCommandList(0, m_CommandListType, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList))
	);

	return commandList;
}


// This method returns a command list that can be directly used to issue GPU drawing (or dispatch) commands.
//		The command list will be in the recording state so there is no need for the user to reset the command list
//		before using it. A command allocator will already be associated with the command list but the CommandQueue 
//		class needs a way to keep track of which command allocator is associated with which command list. Since there
//		is no way to directly query the command allocator that was used to reset the command list, a pointer 
//		to the command allocator is stored in the private data space of the command list.
ComPtr<ID3D12GraphicsCommandList2> CommandQueue::GetCommandList()
{
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ComPtr<ID3D12GraphicsCommandList2> commandList;

	// Before the command list can be reset, an unused command allocator is required 
	//		as CommandList->Reset requires CommandAllocator as a parameter.
	// The Command allocator can be reused as long as it is not currently “in-flight”
	//      on the command queue.
	if (!m_CommandAllocatorQueue.empty() && IsFenceComplete(m_CommandAllocatorQueue.front().fenceValue))
	{
		commandAllocator = m_CommandAllocatorQueue.front().commandAllocator;
		m_CommandAllocatorQueue.pop();

		ThrowIfFailed(commandAllocator->Reset());
	}
	else
	{
		commandAllocator = CreateCommandAllocator();
	}

	// With a valid command allocator, the command list is created next.
	if (!m_CommandListQueue.empty())
	{
		commandList = m_CommandListQueue.front();
		m_CommandListQueue.pop();

		ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
	}
	else
	{
		commandList = CreateCommandList(commandAllocator);
	}

	// Associate the command allocator with the command list so that it can be
	//		retrieved when the command list is executed.
	// When the command list will be executed, the command allocator can be retrieved
	//		from the command list and pushed to the back of the command allocator QUEUE.
	//
	// !NOTE! 
	//		When assigning a COM object to the private data of an ID3D12Object object using the 
	//		ID3D12Object::SetPrivateDataInterface method, the internal reference counter 
	//		of the assigned COM object is incremented. The ref counter of the assigned COM 
	//		object is only decremented if either the owning ID3D12Object object is destroyed 
	//		or the instance of the COM object with the same interface is replaced with another 
	//		COM object of the same interface or a NULL pointer.
	ThrowIfFailed(commandList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), commandAllocator.Get()));

	return commandList;
}


UINT64 CommandQueue::ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList2> commandList)
{
	commandList->Close();

	// Be aware that retrieving a COM pointer of a COM object associated with the private data
	// of the ID3D12Object object will also increment the reference counter of that COM object.
	ID3D12CommandAllocator* commandAllocator;
	UINT dataSize = sizeof(commandAllocator);
	ThrowIfFailed(commandList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator));

	ID3D12CommandList* const ppCommandLists[] = {
		commandList.Get()
	};

	m_d3d12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
	uint64_t fenceValue = Signal();

	m_CommandAllocatorQueue.emplace(CommandAllocatorEntry{ fenceValue, commandAllocator });
	m_CommandListQueue.push(commandList);

	// The temporary pointer to the command allocator needs to be DECREMENTED by 
	//		releasing the COM pointer.
	// The ownership of the command allocator has been transferred to the ComPtr
	//		in the command allocator queue. It is safe to release the reference 
	//		in this temporary COM pointer here.
	commandAllocator->Release();

	return fenceValue;
}


ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const
{
	return m_d3d12CommandQueue;
}
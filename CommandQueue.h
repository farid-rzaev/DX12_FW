#pragma once
#include <d3d12.h>
#include <wrl.h>    // For Microsoft::WRL::ComPtr
#include <queue>

using Microsoft::WRL::ComPtr;

class CommandQueue
{
public:
	CommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	UINT64 Signal();
	bool IsFenceComplete(UINT64 fenceValue);
	void WaitForFenceValue(UINT64 fenceValue);
	void Flush();

	// Get an available command list from the command queue.
	ComPtr<ID3D12GraphicsCommandList2> GetCommandList();
	UINT64 ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList2> commandList);
	ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const;

protected:
	ComPtr<ID3D12CommandAllocator> CreateCommandAllocator();
	ComPtr<ID3D12GraphicsCommandList2> CreateCommandList(ComPtr<ID3D12CommandAllocator> allocator);

private /*helpers*/:
	// Keep track of command allocators that are "in-flight":
	//		Struct is used to associate a fence value with a command allocator.
	//		It is known that - the command list can be reused immediately after it has been executed
	//		on the command queue, but the command allocator cannot be reused unless the commands 
	//		stored in the command allocator have finished executing on the command queue.
	//		To guarantee that the command allocator is no longer “in-flight” on the command queue, 
	//		a fence value is signaled on the command queue and that fence value 
	//		(together with the associated command allocator) is stored for later reuse.
	struct CommandAllocatorEntry
	{
		UINT64 fenceValue;
		ComPtr<ID3D12CommandAllocator> commandAllocator;
	};
	typedef ComPtr<ID3D12GraphicsCommandList2> CommandListEntry;

	using CommandAllocatorQueue = std::queue<CommandAllocatorEntry>;
	using CommandListQueue = std::queue<CommandListEntry>;

private /*main*/:
	// There is no need to associate a fence value with the command lists since 
	// they can be reused right after they have been executed on the command queue.
	CommandListQueue	                        m_CommandListQueue;
	CommandAllocatorQueue                       m_CommandAllocatorQueue;

	// CommandQueue
	D3D12_COMMAND_LIST_TYPE m_CommandListType;
	ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue;

	// Synchronization objects
	ComPtr<ID3D12Fence> m_d3d12Fence;
	UINT64 m_FenceValue = 0;
	HANDLE m_FenceEvent;

	// Device
	ComPtr<ID3D12Device2> m_d3d12Device;
};


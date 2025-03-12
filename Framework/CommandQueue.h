#pragma once

#include <d3d12.h>
#include <wrl.h>	// ComPtr

#include <memory>	// shared_ptr
#include <queue>


using Microsoft::WRL::ComPtr;


class Application;


class CommandQueue
{
public:
	CommandQueue(std::shared_ptr<Application> app, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	uint64_t Signal();
	bool IsFenceComplete(uint64_t fenceValue);
	void WaitForFenceValue(uint64_t fenceValue);
	void Flush();

	// Returns the fence value to wait for this command list.
	uint64_t ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList2> commandList);
	
	// Get an available command list from the command queue.
	ComPtr<ID3D12GraphicsCommandList2> GetCommandList();
	ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;

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
		uint64_t fenceValue;
		ComPtr<ID3D12CommandAllocator> commandAllocator;
	};

	using CommandAllocatorQueue = std::queue<CommandAllocatorEntry>;
	using CommandListQueue = std::queue< ComPtr<ID3D12GraphicsCommandList2> >;

private /*main*/:
	// There is no need to associate a fence value with the command lists since 
	// they can be reused right after they have been executed on the command queue.
	CommandAllocatorQueue			m_CommandAllocatorQueue;
	CommandListQueue				m_CommandListQueue;

	// CommandQueue
	D3D12_COMMAND_LIST_TYPE			m_CommandListType;
	ComPtr<ID3D12CommandQueue>		m_d3d12CommandQueue;

	// Synchronization objects
	ComPtr<ID3D12Fence>				m_d3d12Fence;
	HANDLE							m_FenceEvent;
	uint64_t						m_FenceValue = 0;

	// Application
	std::shared_ptr<Application>	m_application;
};


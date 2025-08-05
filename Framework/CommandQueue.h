#pragma once

#include <d3d12.h>
#include <wrl.h>	// ComPtr

#include <memory>	// shared_ptr
#include <queue>

#include <External/Threading/ThreadSafeQueue.h>

class Application;
class CommandList;

class CommandQueue
{
public:
	CommandQueue(std::shared_ptr<Application> app, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	uint64_t Signal();
	bool IsFenceComplete(uint64_t fenceValue);
	void WaitForFenceValue(uint64_t fenceValue);
	void Flush();

	// Wait for another command queue to finish.
	void Wait(const CommandQueue& other);

	// Returns the fence value to wait for this command list.
	uint64_t ExecuteCommandList(std::shared_ptr<CommandList> commandList);
	uint64_t ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList> >& commandLists);
	
	// Get an available command list from the command queue.
	std::shared_ptr<CommandList> GetCommandList();
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;

private /*helpers*/:

	// Free any command lists that are finished processing on the command queue.
	void ProccessInFlightCommandLists();

	// Keep track of command allocators that are "in-flight"
	// The first member is the fence value to wait for, the second is the 
	// a shared pointer to the "in-flight" command list.
	using CommandListEntry = std::tuple<uint64_t, std::shared_ptr<CommandList>>;

private /*main*/:

	// CommandQueue
	D3D12_COMMAND_LIST_TYPE							m_CommandListType;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>		m_d3d12CommandQueue;

	// Synchronization objects
	Microsoft::WRL::ComPtr<ID3D12Fence>				m_d3d12Fence;
	std::atomic_uint64_t							m_FenceValue = 0;

	// Command Lists
	ThreadSafeQueue<CommandListEntry>				m_InFlightCommandLists;
	ThreadSafeQueue<std::shared_ptr<CommandList>>	m_AvailableCommandLists;

	// A thread to process in-flight command lists.
	std::thread										m_ProcessInFlightCommandListsThread;
	std::atomic_bool								m_bProcessInFlightCommandLists;
	std::mutex										m_ProcessInFlightCommandListsThreadMutex;
	std::condition_variable							m_ProcessInFlightCommandListsThreadCV;

	// Application
	std::shared_ptr<Application>					m_Application = nullptr;
};


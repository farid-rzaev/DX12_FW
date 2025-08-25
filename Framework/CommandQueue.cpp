#include "CommandQueue.h"

#include <Framework/Application.h>
#include <Framework/CommandList.h>
#include <Framework/ResourceStateTracker.h>

#include <Framework/3RD_Party/Helpers.h>

#include <cassert>

#include <thread>
#include <mutex>


// TODO:
// 1. Consider removing m_ProcessInFlightCommandListsThreadCV - it adds complexity without a clear benefit; We already have exclusive locks that are sufficient;
// 2. And in the case when Flush() was called before ProccessInFlightCommandLists() to drain all the m_InFlightCommandLists just modify Flush():
// 
//		void CommandQueue::Flush()
//		{
//			while (!m_InFlightCommandLists.Empty())
//			{
//				std::this_thread::sleep_for(std::chrono::milliseconds(1));
//			}
//		
//			std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex);
//		
//			WaitForFenceValue(m_FenceValue);
//		}


CommandQueue::CommandQueue(std::shared_ptr<Application> app, D3D12_COMMAND_LIST_TYPE type)
	: m_Application(app)
	, m_CommandListType(type)
	, m_FenceValue(0)
	, m_bProcessInFlightCommandLists(true)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	auto device = Application::GetDevice();

	ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_d3d12CommandQueue)));
	ThrowIfFailed(device->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12Fence)));

	switch (type)
	{
		case D3D12_COMMAND_LIST_TYPE_COPY:
			m_d3d12CommandQueue->SetName(L"Copy Command Queue");
			break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE:
			m_d3d12CommandQueue->SetName(L"Compute Command Queue");
			break;
		case D3D12_COMMAND_LIST_TYPE_DIRECT:
			m_d3d12CommandQueue->SetName(L"Direct Command Queue");
			break;
	}

	m_ProcessInFlightCommandListsThread = std::thread(&CommandQueue::ProccessInFlightCommandLists, this);
}


CommandQueue::~CommandQueue()
{
	m_bProcessInFlightCommandLists = false;
	m_ProcessInFlightCommandListsThread.join();
}


uint64_t CommandQueue::Signal() {
	// Signal() is being called from multiple threads (CommandQueue and Window threads) concurently, so m_FenceValue has to be atomic
	uint64_t fenceValue = ++m_FenceValue;
	m_d3d12CommandQueue->Signal(m_d3d12Fence.Get(), fenceValue);
	return fenceValue;
}


bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
	return m_d3d12Fence->GetCompletedValue() >= fenceValue;
}


void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
	if (!IsFenceComplete(fenceValue)) 
	{
		auto event = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		assert(event && "Failed to create fence event handle.");

		m_d3d12Fence->SetEventOnCompletion(fenceValue, event);
		::WaitForSingleObject(event, DWORD_MAX);

		::CloseHandle(event);
	}
}


void CommandQueue::Flush()
{
	std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex);

	// Block this thread and wait for notification from a ProccessInFlightCommandLists() thread and Predicate
	m_ProcessInFlightCommandListsThreadCV.wait(lock, [this] { return m_InFlightCommandLists.Empty(); });

	// If the command queue was signaled directly using the CommandQueue::Signal() then the fence value
	// of the command queue might be higher than the fence value of any of the executed command lists.
	WaitForFenceValue(m_FenceValue);
}


std::shared_ptr<CommandList> CommandQueue::GetCommandList()
{
	std::shared_ptr<CommandList> commandList;

	// If there is a command list on the queue.
	if (!m_AvailableCommandLists.Empty())
	{
		m_AvailableCommandLists.TryPop(commandList);
	}
	else
	{
		// Otherwise create a new command list.
		commandList = std::make_shared<CommandList>(m_Application, m_CommandListType);
	}

	return commandList;
}


uint64_t CommandQueue::ExecuteCommandList(std::shared_ptr<CommandList> commandList)
{
	return ExecuteCommandLists(std::vector<std::shared_ptr<CommandList> >({ commandList }));
}


uint64_t CommandQueue::ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList> >& commandLists)
{
	ResourceStateTracker::Lock();

	// Command lists that need to put back on the command list queue.
	std::vector<std::shared_ptr<CommandList> > toBeQueued;
	toBeQueued.reserve(commandLists.size() * 2);        // 2x since each command list will have a pending command list.

	// Generate mips command lists:
	// - storing them separatemy as they will be executed in a different (COMPUTE) queue
	std::vector<std::shared_ptr<CommandList> > generateMipsCommandLists;
	generateMipsCommandLists.reserve(commandLists.size());

	// Command lists that need to be executed.
	std::vector<ID3D12CommandList*> d3d12CommandLists;
	d3d12CommandLists.reserve(commandLists.size() * 2); // 2x since each command list will have a pending command list.

	for (auto commandList : commandLists)
	{
		auto pendingCommandList = GetCommandList();
		bool hasPendingBarriers = commandList->Close(*pendingCommandList);
		pendingCommandList->Close();
		// If there are no pending barriers on the pending command list, there is no reason to 
		// execute an empty command list on the command queue.
		if (hasPendingBarriers)
		{
			d3d12CommandLists.push_back(pendingCommandList->GetGraphicsCommandList().Get());
		}
		d3d12CommandLists.push_back(commandList->GetGraphicsCommandList().Get());

		toBeQueued.push_back(pendingCommandList);
		toBeQueued.push_back(commandList);

		auto generateMipsCommandList = commandList->GetGenerateMipsCommandList();
		if (generateMipsCommandList)
		{
			generateMipsCommandLists.push_back(generateMipsCommandList);
		}
	}

	UINT numCommandLists = static_cast<UINT>(d3d12CommandLists.size());
	m_d3d12CommandQueue->ExecuteCommandLists(numCommandLists, d3d12CommandLists.data());
	uint64_t fenceValue = Signal();

	ResourceStateTracker::Unlock();

	// Queue command lists for reuse.
	for (auto commandList : toBeQueued)
	{
		m_InFlightCommandLists.Push({ fenceValue, commandList });
	}

	// If there are any command lists that generate mips then execute those
	// after the initial resource command lists have finished.
	if (generateMipsCommandLists.size() > 0)
	{
		auto computeQueue = m_Application->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		computeQueue->Wait(*this);
		computeQueue->ExecuteCommandLists(generateMipsCommandLists);
	}

	return fenceValue;
}


void CommandQueue::Wait(const CommandQueue& other)
{
	m_d3d12CommandQueue->Wait(other.m_d3d12Fence.Get(), other.m_FenceValue);
}


ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const
{
	return m_d3d12CommandQueue;
}


void CommandQueue::ProccessInFlightCommandLists()
{
	std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex, std::defer_lock);

	while (m_bProcessInFlightCommandLists)
	{
		CommandListEntry commandListEntry;

		lock.lock();
		{
			while (m_InFlightCommandLists.TryPop(commandListEntry))
			{
				auto fenceValue = std::get<0>(commandListEntry);
				auto commandList = std::get<1>(commandListEntry);

				WaitForFenceValue(fenceValue);

				commandList->Reset();

				m_AvailableCommandLists.Push(commandList);
			}
		}
		lock.unlock();

		m_ProcessInFlightCommandListsThreadCV.notify_one();

		std::this_thread::yield();
	}
}
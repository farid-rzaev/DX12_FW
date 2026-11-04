#include "ResourceStateTracker.h"

#include "Application.h"
#include "CommandList.h"
#include "Material/Resource.h"

#include <Framework/3RD_Party/D3D/d3dx12.h>

#include <algorithm>
#include <assert.h>

namespace
{
    UINT GetNumSubresources(ID3D12Resource* resource)
    {
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            return 1;
        }

        const UINT arraySize  = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
        const UINT planeCount = std::max<UINT>(D3D12GetFormatPlaneCount(Application::Get().GetDevice().Get(), desc.Format), 1);
        return desc.MipLevels * arraySize * planeCount;
    }
}

// Static definitions.
std::mutex ResourceStateTracker::ms_GlobalMutex;
bool ResourceStateTracker::ms_IsLocked = false;
ResourceStateTracker::ResourceStateMap ResourceStateTracker::ms_GlobalResourceState;

ResourceStateTracker::ResourceStateTracker()
{}

ResourceStateTracker::~ResourceStateTracker()
{}

void ResourceStateTracker::ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier)
{
    if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
    {
        const D3D12_RESOURCE_TRANSITION_BARRIER& transitionBarrier = barrier.Transition;

        // First check if there is already a known "final" state for the given resource.
        // If there is, the resource has been used on the command list before and
        // already has a known state within the command list execution.
        const auto iter = m_FinalResourceState.find(transitionBarrier.pResource);
        if (iter != m_FinalResourceState.end())
        {
            auto& resourceState = iter->second;
            // If the known final state of the resource is different...
            if ( transitionBarrier.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                 !resourceState.SubresourceState.empty() )
            {
                // Transition every subresource that is in a different state than StateAfter -
                // including the ones without an individual state entry.
                const UINT numSubresources = GetNumSubresources( transitionBarrier.pResource );
                for ( UINT subresource = 0; subresource < numSubresources; ++subresource )
                {
                    D3D12_RESOURCE_BARRIER newBarrier = barrier;
                    newBarrier.Transition.Subresource = subresource;

                    const auto subresourceIter = resourceState.SubresourceState.find( subresource );
                    if ( subresourceIter == resourceState.SubresourceState.end() && !resourceState.IsStateKnown )
                    {
                        // Not used on this command list yet - resolved against the global state before execution.
                        m_PendingResourceBarriers.push_back( newBarrier );
                        continue;
                    }

                    const D3D12_RESOURCE_STATES finalState = resourceState.GetSubresourceState( subresource );
                    if ( transitionBarrier.StateAfter != finalState )
                    {
                        newBarrier.Transition.StateBefore = finalState;
                        m_ResourceBarriers.push_back( newBarrier );
                    }
                }
            }
            else if ( !resourceState.IsStateKnown &&
                      resourceState.SubresourceState.find( transitionBarrier.Subresource ) == resourceState.SubresourceState.end() )
            {
                // Other subresources were used on this command list, but this one wasn't yet.
                m_PendingResourceBarriers.push_back( barrier );
            }
            else
            {
                auto finalState = resourceState.GetSubresourceState( transitionBarrier.Subresource );
                if ( transitionBarrier.StateAfter != finalState )
                {
                    // Push a new transition barrier with the correct before state.
                    D3D12_RESOURCE_BARRIER newBarrier = barrier;
                    newBarrier.Transition.StateBefore = finalState;
                    m_ResourceBarriers.push_back( newBarrier );
                }
            }
        }
        else // In this case, the resource is being used on the command list for the first time. 
        {
            // Add a pending barrier. The pending barriers will be resolved
            // before the command list is executed on the command queue.
            m_PendingResourceBarriers.push_back(barrier);

            // Only this subresource's state becomes known on this command list.
            if ( transitionBarrier.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES )
            {
                m_FinalResourceState[transitionBarrier.pResource].IsStateKnown = false;
            }
        }

        // Push the final known state (possibly replacing the previously known state for the subresource).
        m_FinalResourceState[transitionBarrier.pResource].SetSubresourceState(transitionBarrier.Subresource, transitionBarrier.StateAfter);
    }
    else
    {
        // Just push non-transition barriers to the resource barriers array.
        m_ResourceBarriers.push_back(barrier);
    }
}

void ResourceStateTracker::TransitionResource( ID3D12Resource* resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource )
{
    if ( resource )
    {
        ResourceBarrier( CD3DX12_RESOURCE_BARRIER::Transition( resource, D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource ) );
    }
}

void ResourceStateTracker::TransitionResource( const Resource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource )
{
    TransitionResource( resource.GetD3D12Resource().Get(), stateAfter, subResource );
}

void ResourceStateTracker::UAVBarrier(const Resource* resource )
{
    ID3D12Resource* pResource = resource != nullptr ? resource->GetD3D12Resource().Get() : nullptr;

    ResourceBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource));
}

void ResourceStateTracker::AliasBarrier(const Resource* resourceBefore, const Resource* resourceAfter)
{
    ID3D12Resource* pResourceBefore = resourceBefore != nullptr ? resourceBefore->GetD3D12Resource().Get() : nullptr;
    ID3D12Resource* pResourceAfter = resourceAfter != nullptr ? resourceAfter->GetD3D12Resource().Get() : nullptr;

    ResourceBarrier(CD3DX12_RESOURCE_BARRIER::Aliasing(pResourceBefore, pResourceAfter));
}

void ResourceStateTracker::FlushResourceBarriers(CommandList& commandList)
{
    UINT numBarriers = static_cast<UINT>(m_ResourceBarriers.size());
    if (numBarriers > 0 )
    {
        auto d3d12CommandList = commandList.GetGraphicsCommandList();
        d3d12CommandList->ResourceBarrier(numBarriers, m_ResourceBarriers.data());
        m_ResourceBarriers.clear();
    }
}

uint32_t ResourceStateTracker::FlushPendingResourceBarriers(CommandList& commandList)
{
    assert(ms_IsLocked);

    // Resolve the pending resource barriers by checking the global state of the 
    // (sub)resources. Add barriers if the pending state and the global state do
    //  not match.
    ResourceBarriers resourceBarriers;
    // Reserve enough space (worst-case, all pending barriers).
    resourceBarriers.reserve(m_PendingResourceBarriers.size());

    for (auto pendingBarrier : m_PendingResourceBarriers)
    {
        if (pendingBarrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)  // Only transition barriers should be pending...
        {
            auto pendingTransition = pendingBarrier.Transition;
            
            const auto& iter = ms_GlobalResourceState.find(pendingTransition.pResource);
            if (iter != ms_GlobalResourceState.end())
            {
                // If all subresources are being transitioned, and there are multiple
                // subresources of the resource that are in a different state...
                auto& resourceState = iter->second;
                if ( pendingTransition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                     !resourceState.SubresourceState.empty() )
                {
                    // Transition all subresources - including the ones without an individual state entry.
                    const UINT numSubresources = GetNumSubresources( pendingTransition.pResource );
                    for ( UINT subresource = 0; subresource < numSubresources; ++subresource )
                    {
                        const D3D12_RESOURCE_STATES globalState = resourceState.GetSubresourceState( subresource );
                        if ( pendingTransition.StateAfter != globalState )
                        {
                            D3D12_RESOURCE_BARRIER newBarrier = pendingBarrier;
                            newBarrier.Transition.Subresource = subresource;
                            newBarrier.Transition.StateBefore = globalState;
                            resourceBarriers.push_back( newBarrier );
                        }
                    }
                }
                else
                {
                    // No (sub)resources need to be transitioned. Just add a single transition barrier (if needed).
                    auto globalState = ( iter->second ).GetSubresourceState( pendingTransition.Subresource );
                    if ( pendingTransition.StateAfter != globalState )
                    {
                        // Fix-up the before state based on current global state of the resource.
                        pendingBarrier.Transition.StateBefore = globalState;
                        resourceBarriers.push_back( pendingBarrier );
                    }
                }
            }
        }
    }

    UINT numBarriers = static_cast<UINT>(resourceBarriers.size());
    if (numBarriers > 0 )
    {
        auto d3d12CommandList = commandList.GetGraphicsCommandList();
        d3d12CommandList->ResourceBarrier(numBarriers, resourceBarriers.data());
    }

    m_PendingResourceBarriers.clear();

    return numBarriers;
}

void ResourceStateTracker::CommitFinalResourceStates()
{
    assert(ms_IsLocked);

    // Commit final resource states to the global resource state array (map).
    for (const auto& resourceState : m_FinalResourceState)
    {
        if (resourceState.second.IsStateKnown)
        {
            ms_GlobalResourceState[resourceState.first] = resourceState.second;
        }
        else
        {
            // Only some subresources were used - keep the global state of the others.
            auto& globalState = ms_GlobalResourceState[resourceState.first];
            for (const auto& subresourceState : resourceState.second.SubresourceState)
            {
                globalState.SetSubresourceState(subresourceState.first, subresourceState.second);
            }
        }
    }

    m_FinalResourceState.clear();
}

void ResourceStateTracker::Reset()
{
    // Reset the pending, current, and final resource states.
    m_PendingResourceBarriers.clear();
    m_ResourceBarriers.clear();
    m_FinalResourceState.clear();
}

void ResourceStateTracker::Lock()
{
    ms_GlobalMutex.lock();
    ms_IsLocked = true;
}

void ResourceStateTracker::Unlock()
{
    ms_GlobalMutex.unlock();
    ms_IsLocked = false;
}

void ResourceStateTracker::AddGlobalResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
{
    if ( resource != nullptr )
    {
        std::lock_guard<std::mutex> lock(ms_GlobalMutex);
        ms_GlobalResourceState[resource].SetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state);
    }
}

void ResourceStateTracker::RemoveGlobalResourceState(ID3D12Resource* resource)
{
    if ( resource != nullptr )
    {
        std::lock_guard<std::mutex> lock(ms_GlobalMutex);
        ms_GlobalResourceState.erase(resource);
    }
}

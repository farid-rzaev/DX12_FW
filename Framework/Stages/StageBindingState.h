#pragma once

#include <d3d12.h>

#include <string>

// Static description of one required binding (one per resource array element). Owned by PipelineStage.
struct StageBindingDesc
{
    std::string Name;
    uint32_t    ArrayIndex = 0;
};

// Mip x array-slice range touched by a bound view (buffers: a single subresource).
struct StageSubresourceRange
{
    UINT FirstMip   = 0;
    UINT MipCount   = 1;
    UINT FirstSlice = 0;
    UINT SliceCount = 1;

    // Of the whole resource - needed to compute subresource indices.
    UINT MipLevels  = 1;
    UINT ArraySize  = 1;

    bool CoversWholeResource() const
    {
        return FirstMip == 0 && MipCount == MipLevels && FirstSlice == 0 && SliceCount == ArraySize;
    }
};

// Per-command-list state of one required binding, indexed like PipelineStage's StageBindingDescs.
// Reset by every PipelineStage::Bind, so it is kept trivially copyable.
struct StageBinding
{
    bool                  Bound            = false;

    // SRV/UAV only (null for CBVs). Kept alive by the command list's tracked objects.
    ID3D12Resource*       Resource         = nullptr;
    StageSubresourceRange Subresources;
    D3D12_RESOURCE_STATES State            = D3D12_RESOURCE_STATE_COMMON;
    bool                  FixedUploadState = false;   // upload-heap resources can't be transitioned
};

// One subresource access collected by CommandList::ValidateStage (scratch storage).
struct StageSubresourceAccess
{
    ID3D12Resource*       Resource;
    UINT                  Subresource;
    UINT                  NumSubresources;   // of the whole resource
    D3D12_RESOURCE_STATES State;
};

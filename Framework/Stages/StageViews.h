#pragma once

#include <Framework/Application.h>
#include <Framework/Material/Resource.h>
#include <Framework/Material/StructuredBuffer.h>
#include <Framework/Stages/StageBindingState.h>
#include <Framework/3RD_Party/D3D/d3dx12.h>

#include <stdexcept>
#include <unordered_map>

// Internal PipelineStage helpers: build default views from the reflected shader dimension
// and derive the subresources a view touches. The same view desc is used both for the
// descriptor and for the state transitions, so they can't describe different regions.
namespace StageViews
{
    // Depth formats can't be viewed directly - use the matching color format.
    inline DXGI_FORMAT ViewFormat(DXGI_FORMAT format)
    {
        if (format == DXGI_FORMAT_R32_TYPELESS || format == DXGI_FORMAT_D32_FLOAT)
        {
            return DXGI_FORMAT_R32_FLOAT;
        }
        return format;
    }

    inline D3D12_RESOURCE_DIMENSION ResourceDimensionFor(D3D_SRV_DIMENSION shaderDimension)
    {
        switch (shaderDimension)
        {
        case D3D_SRV_DIMENSION_BUFFER:         return D3D12_RESOURCE_DIMENSION_BUFFER;
        case D3D_SRV_DIMENSION_TEXTURE1D:
        case D3D_SRV_DIMENSION_TEXTURE1DARRAY: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case D3D_SRV_DIMENSION_TEXTURE3D:      return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        default:                               return D3D12_RESOURCE_DIMENSION_TEXTURE2D;   // 2D, 2D arrays, MS, cubes
        }
    }

    inline D3D12_UAV_DIMENSION UAVDimensionFor(D3D_SRV_DIMENSION shaderDimension)
    {
        switch (shaderDimension)
        {
        case D3D_SRV_DIMENSION_BUFFER:         return D3D12_UAV_DIMENSION_BUFFER;
        case D3D_SRV_DIMENSION_TEXTURE1D:      return D3D12_UAV_DIMENSION_TEXTURE1D;
        case D3D_SRV_DIMENSION_TEXTURE1DARRAY: return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
        case D3D_SRV_DIMENSION_TEXTURE2D:      return D3D12_UAV_DIMENSION_TEXTURE2D;
        case D3D_SRV_DIMENSION_TEXTURE2DARRAY: return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        case D3D_SRV_DIMENSION_TEXTURE3D:      return D3D12_UAV_DIMENSION_TEXTURE3D;
        default:                               throw std::runtime_error("PipelineStage: unsupported UAV dimension.");
        }
    }

    // Queried on every SetSRV/SetUAV - cached (per thread) to avoid a driver call each time.
    inline UINT8 FormatPlaneCount(DXGI_FORMAT format)
    {
        thread_local std::unordered_map<DXGI_FORMAT, UINT8> planeCounts;

        auto iter = planeCounts.find(format);
        if (iter == planeCounts.end())
        {
            auto device = Application::Get().GetDevice();
            iter = planeCounts.emplace(format, D3D12GetFormatPlaneCount(device.Get(), format)).first;
        }
        return iter->second;
    }

    // Check that a resource can back a view of the reflected shader dimension.
    inline void ValidateResource(const D3D12_RESOURCE_DESC& resourceDesc, D3D_SRV_DIMENSION shaderDimension)
    {
        if (resourceDesc.Dimension != ResourceDimensionFor(shaderDimension))
        {
            throw std::runtime_error("PipelineStage: resource dimension does not match the shader view.");
        }

        // Early exits cuz buffers don't have mipmap levels or array slices � the validation checks that follow are only meaningful for texture resources.
        if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            return;
        }

        const bool isMultisampledView = shaderDimension == D3D_SRV_DIMENSION_TEXTURE2DMS ||
                                        shaderDimension == D3D_SRV_DIMENSION_TEXTURE2DMSARRAY;
        if (isMultisampledView != (resourceDesc.SampleDesc.Count > 1))
        {
            throw std::runtime_error("PipelineStage: resource sample count does not match the shader view.");
        }

        // Transitions are tracked per mip/slice only - multi-plane formats (e.g. D24S8) would need per-plane tracking.
        if (FormatPlaneCount(resourceDesc.Format) != 1)
        {
            throw std::runtime_error("PipelineStage: only single-plane resource formats are supported.");
        }
    }

    // Validated mip range x array-slice range. mipCount == (UINT)-1 means "all remaining mips".
    inline StageSubresourceRange SelectSubresources(const D3D12_RESOURCE_DESC& resourceDesc,
                                                    UINT firstMip, UINT mipCount,
                                                    UINT firstSlice, UINT sliceCount,
                                                    UINT planeSlice = 0)
    {
        const UINT arraySize = resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : resourceDesc.DepthOrArraySize;

        if (firstMip >= resourceDesc.MipLevels || firstSlice >= arraySize)
        {
            throw std::runtime_error("PipelineStage: view starts outside the resource.");
        }

        if (mipCount == (UINT)-1)
        {
            mipCount = resourceDesc.MipLevels - firstMip;
        }

        if (mipCount == 0 || mipCount > resourceDesc.MipLevels - firstMip ||
            sliceCount == 0 || sliceCount > arraySize - firstSlice)
        {
            throw std::runtime_error("PipelineStage: view range exceeds the resource.");
        }

        if (planeSlice != 0)
        {
            throw std::runtime_error("PipelineStage: non-zero view planes are not supported yet.");
        }

        StageSubresourceRange range;
        range.FirstMip   = firstMip;
        range.MipCount   = mipCount;
        range.FirstSlice = firstSlice;
        range.SliceCount = sliceCount;
        range.MipLevels  = resourceDesc.MipLevels;
        range.ArraySize  = arraySize;
        return range;
    }

    // Default SRV covering the whole resource, shaped to the reflected shader dimension.
    inline D3D12_SHADER_RESOURCE_VIEW_DESC DefaultSRV(const Resource& resource, D3D_SRV_DIMENSION shaderDimension)
    {
        const D3D12_RESOURCE_DESC resourceDesc = resource.GetD3D12ResourceDesc();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = ViewFormat(resourceDesc.Format);
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension           = static_cast<D3D12_SRV_DIMENSION>(shaderDimension);   // same enum values

        switch (srvDesc.ViewDimension)
        {
        case D3D12_SRV_DIMENSION_BUFFER:
        {
            const StructuredBuffer* structuredBuffer = dynamic_cast<const StructuredBuffer*>(&resource);
            if (!structuredBuffer)
            {
                throw std::runtime_error("PipelineStage: non-structured buffers require an explicit SRV desc.");
            }
            srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.NumElements         = static_cast<UINT>(structuredBuffer->GetNumElements());
            srvDesc.Buffer.StructureByteStride = static_cast<UINT>(structuredBuffer->GetElementSize());
            break;
        }
        case D3D12_SRV_DIMENSION_TEXTURE1D:
            srvDesc.Texture1D.MipLevels = resourceDesc.MipLevels;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
            srvDesc.Texture1DArray.MipLevels = resourceDesc.MipLevels;
            srvDesc.Texture1DArray.ArraySize = resourceDesc.DepthOrArraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2D:
            srvDesc.Texture2D.MipLevels = resourceDesc.MipLevels;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            srvDesc.Texture2DArray.MipLevels = resourceDesc.MipLevels;
            srvDesc.Texture2DArray.ArraySize = resourceDesc.DepthOrArraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMS:
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
            srvDesc.Texture2DMSArray.ArraySize = resourceDesc.DepthOrArraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            srvDesc.Texture3D.MipLevels = resourceDesc.MipLevels;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
            srvDesc.TextureCube.MipLevels = resourceDesc.MipLevels;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            srvDesc.TextureCubeArray.MipLevels = resourceDesc.MipLevels;
            srvDesc.TextureCubeArray.NumCubes  = resourceDesc.DepthOrArraySize / 6;
            break;
        default:
            throw std::runtime_error("PipelineStage: unsupported SRV dimension.");
        }

        return srvDesc;
    }

    inline StageSubresourceRange Subresources(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
    {
        switch (srvDesc.ViewDimension)
        {
        case D3D12_SRV_DIMENSION_BUFFER:
            if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                throw std::runtime_error("PipelineStage: buffer view on a non-buffer resource.");
            }
            return StageSubresourceRange();
        case D3D12_SRV_DIMENSION_TEXTURE1D:
        {
            const auto& view = srvDesc.Texture1D;
            return SelectSubresources(resourceDesc, view.MostDetailedMip, view.MipLevels, 0, 1);
        }
        case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
        {
            const auto& view = srvDesc.Texture1DArray;
            return SelectSubresources(resourceDesc, view.MostDetailedMip, view.MipLevels, view.FirstArraySlice, view.ArraySize);
        }
        case D3D12_SRV_DIMENSION_TEXTURE2D:
        {
            const auto& view = srvDesc.Texture2D;
            return SelectSubresources(resourceDesc, view.MostDetailedMip, view.MipLevels, 0, 1, view.PlaneSlice);
        }
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
        {
            const auto& view = srvDesc.Texture2DArray;
            return SelectSubresources(resourceDesc, view.MostDetailedMip, view.MipLevels, view.FirstArraySlice, view.ArraySize, view.PlaneSlice);
        }
        case D3D12_SRV_DIMENSION_TEXTURE2DMS:
            return SelectSubresources(resourceDesc, 0, 1, 0, 1);
        case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
        {
            const auto& view = srvDesc.Texture2DMSArray;
            return SelectSubresources(resourceDesc, 0, 1, view.FirstArraySlice, view.ArraySize);
        }
        case D3D12_SRV_DIMENSION_TEXTURE3D:
        {
            const auto& view = srvDesc.Texture3D;
            return SelectSubresources(resourceDesc, view.MostDetailedMip, view.MipLevels, 0, 1);
        }
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
        {
            const auto& view = srvDesc.TextureCube;
            return SelectSubresources(resourceDesc, view.MostDetailedMip, view.MipLevels, 0, 6);
        }
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        {
            const auto& view = srvDesc.TextureCubeArray;
            return SelectSubresources(resourceDesc, view.MostDetailedMip, view.MipLevels, view.First2DArrayFace, view.NumCubes * 6);
        }
        default:
            throw std::runtime_error("PipelineStage: unsupported SRV dimension.");
        }
    }

    // Default UAV for mip 0 (all slices), shaped to the reflected shader dimension.
    inline D3D12_UNORDERED_ACCESS_VIEW_DESC DefaultUAV(const Resource& resource, D3D_SRV_DIMENSION shaderDimension)
    {
        const D3D12_RESOURCE_DESC resourceDesc = resource.GetD3D12ResourceDesc();

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format        = ViewFormat(resourceDesc.Format);
        uavDesc.ViewDimension = UAVDimensionFor(shaderDimension);

        switch (uavDesc.ViewDimension)
        {
        case D3D12_UAV_DIMENSION_BUFFER:
        {
            const StructuredBuffer* structuredBuffer = dynamic_cast<const StructuredBuffer*>(&resource);
            if (!structuredBuffer)
            {
                throw std::runtime_error("PipelineStage: non-structured buffers require an explicit UAV desc.");
            }
            uavDesc.Format                     = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.NumElements         = static_cast<UINT>(structuredBuffer->GetNumElements());
            uavDesc.Buffer.StructureByteStride = static_cast<UINT>(structuredBuffer->GetElementSize());
            break;
        }
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
            uavDesc.Texture1DArray.ArraySize = resourceDesc.DepthOrArraySize;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
            uavDesc.Texture2DArray.ArraySize = resourceDesc.DepthOrArraySize;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            uavDesc.Texture3D.WSize = (UINT)-1;
            break;
        default:
            break;
        }

        return uavDesc;
    }

    inline StageSubresourceRange Subresources(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc)
    {
        switch (uavDesc.ViewDimension)
        {
        case D3D12_UAV_DIMENSION_BUFFER:
            if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                throw std::runtime_error("PipelineStage: buffer view on a non-buffer resource.");
            }
            return StageSubresourceRange();
        case D3D12_UAV_DIMENSION_TEXTURE1D:
            return SelectSubresources(resourceDesc, uavDesc.Texture1D.MipSlice, 1, 0, 1);
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
        {
            const auto& view = uavDesc.Texture1DArray;
            return SelectSubresources(resourceDesc, view.MipSlice, 1, view.FirstArraySlice, view.ArraySize);
        }
        case D3D12_UAV_DIMENSION_TEXTURE2D:
        {
            const auto& view = uavDesc.Texture2D;
            return SelectSubresources(resourceDesc, view.MipSlice, 1, 0, 1, view.PlaneSlice);
        }
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
        {
            const auto& view = uavDesc.Texture2DArray;
            return SelectSubresources(resourceDesc, view.MipSlice, 1, view.FirstArraySlice, view.ArraySize, view.PlaneSlice);
        }
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            return SelectSubresources(resourceDesc, uavDesc.Texture3D.MipSlice, 1, 0, 1);
        default:
            throw std::runtime_error("PipelineStage: unsupported UAV dimension.");
        }
    }
}

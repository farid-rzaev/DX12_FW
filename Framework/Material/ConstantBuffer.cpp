#include "ConstantBuffer.h"

#include <Framework/Application.h>

#include <External/D3D/d3dx12.h>
#include <External/Helpers.h>

ConstantBuffer::ConstantBuffer(std::shared_ptr<Application> app, const std::wstring& name)
    : Buffer(name)
    , m_SizeInBytes(0)
{
    m_ConstantBufferView = app->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

ConstantBuffer::~ConstantBuffer()
{}

void ConstantBuffer::CreateViews(size_t numElements, size_t elementSize)
{
    m_SizeInBytes = numElements * elementSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC d3d12ConstantBufferViewDesc;
    d3d12ConstantBufferViewDesc.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
    d3d12ConstantBufferViewDesc.SizeInBytes = static_cast<UINT>(Math::AlignUp(m_SizeInBytes, 16));

    auto device = Application::GetDevice();

    device->CreateConstantBufferView(&d3d12ConstantBufferViewDesc, m_ConstantBufferView.GetDescriptorHandle());
}

D3D12_CPU_DESCRIPTOR_HANDLE ConstantBuffer::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
    throw std::exception("ConstantBuffer::GetShaderResourceView should not be called.");
}

D3D12_CPU_DESCRIPTOR_HANDLE ConstantBuffer::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
    throw std::exception("ConstantBuffer::GetUnorderedAccessView should not be called.");
}

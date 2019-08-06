#pragma once

#include "Framework/Application.h"

#include <DirectXMath.h>

class Game : public Application
{
// ------------------------------------------------------------------------------------------
//									Function members
// ------------------------------------------------------------------------------------------
public:
	Game(HINSTANCE hInstance, const wchar_t * windowTitle, int width, int height, bool vSync);
	virtual ~Game();

	virtual void Update();
	virtual void Render();
	virtual void Resize(UINT32 width, UINT32 height);

	// Sample
	bool LoadContent(std::wstring shaderBlobPath);
	void UnloadContent();

protected:
	// Create a GPU buffer.
	void UpdateBufferResource(ComPtr<ID3D12GraphicsCommandList2> commandList,
		ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource,
		size_t numElements, size_t elementSize, const void* bufferData,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
	// Resize the depth buffer to match the size of the client area.
	void ResizeDepthBuffer(int width, int height);

	// Helpers
	void TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList, ComPtr<ID3D12Resource> resource, 
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
	void ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
		D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor);
	void ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
		D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth = 1.0f);

// ------------------------------------------------------------------------------------------
//									Data members
// ------------------------------------------------------------------------------------------
private:
	bool m_ContentLoaded;

	// Vertex buffer for the cube.
	ComPtr<ID3D12Resource> m_VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	// Index buffer for the cube.
	ComPtr<ID3D12Resource> m_IndexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

	// Depth buffer.
	ComPtr<ID3D12Resource> m_DepthBuffer;
	// Descriptor heap for depth buffer.
	ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

	// Root signature
	ComPtr<ID3D12RootSignature> m_RootSignature;

	// Pipeline state object.
	ComPtr<ID3D12PipelineState> m_PipelineState;
private:	
	// View Settings
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_ScissorRect;
	float m_FoV;

	// Camera
	DirectX::XMMATRIX m_ModelMatrix;
	DirectX::XMMATRIX m_ViewMatrix;
	DirectX::XMMATRIX m_ProjectionMatrix;

private:	
	// Frame Sync 
	UINT m_CurrentBackBufferIndex;
	UINT64 m_FenceValues[NUM_FRAMES_IN_FLIGHT] = {};
};
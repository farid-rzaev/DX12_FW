#pragma once

#include "Application.h"

#include <DirectXMath.h>

class Game : public Application
{
// ------------------------------------------------------------------------------------------
//									Function members
// ------------------------------------------------------------------------------------------
public:
	Game(HINSTANCE hInstance, const wchar_t * windowTitle, int width, int height, bool vSync);
	virtual ~Game();

protected:
	virtual void Update();
	virtual void Resize(UINT32 width, UINT32 height);
	virtual void Render();
	
	// Sample
	bool LoadContent();
	// Create a GPU buffer.
	void UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
		ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource,
		size_t numElements, size_t elementSize, const void* bufferData,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

	void UnloadContent();

	// Helpers
	void TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList, ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

// ------------------------------------------------------------------------------------------
//									Data members
// ------------------------------------------------------------------------------------------
private:
	bool m_ContentLoaded;

	// Vertex buffer for the cube.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	// Index buffer for the cube.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

	// Depth buffer.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthBuffer;
	// Descriptor heap for depth buffer.
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

private:	
	// View Settings
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_ScissorRect;
	float m_FoV;

private:	
	// Frame Sync 
	UINT m_CurrentBackBufferIndex;
	UINT64 m_FenceValues[NUM_FRAMES_IN_FLIGHT] = {};
};
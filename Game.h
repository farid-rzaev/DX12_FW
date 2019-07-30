#pragma once

#include "Application.h"

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
	void UnloadContent();

	// Helpers
	void TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList, ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

// ------------------------------------------------------------------------------------------
//									Data members
// ------------------------------------------------------------------------------------------
private:
	bool m_ContentLoaded;

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
#pragma once

#include "Application.h"

class Game : public Application
{
public:
	Game(HINSTANCE hInstance, const wchar_t * windowTitle, int width, int height, bool vSync);
	virtual ~Game();

protected:
	//virtual void Render();
	
	bool LoadContent() {};
	void UnloadContent() {};

private:
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_ScissorRect;
	float m_FoV;
};
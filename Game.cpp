#include "Game.h"

Game::Game(HINSTANCE hInstance, const wchar_t * windowTitle, int width, int height, bool vSync) :
	Application(hInstance, windowTitle, width, height, vSync),
	m_ScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX)),
	m_Viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height)),
	m_FoV(45.0f)
{

}
Game::~Game() 
{

}


bool Game::LoadContent() {
	
}



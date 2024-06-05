#pragma once

#include <Framework/Application.h>
#include <Framework/Events/Events.h>

#include <External/IMGUI/imgui.h>

#include <memory>
#include <string>

class Application;

class Game : public Application
{
public:
	// Create DX demo
	Game(HINSTANCE hInstance);
	virtual ~Game();

	// Init DX Runtime
	virtual bool Initialize(const wchar_t* windowTitle,
		int width, int height, bool vSync)				override;

	// Run
	virtual int Run()									override;

protected: 
	// CONTENT
	virtual bool LoadContent()							= 0;
	virtual void UnloadContent()						= 0;

	// Rendering
	virtual void Update()								override;
	virtual void Render()								override;
	virtual void Resize(UINT32 width, UINT32 height)	override;

protected:

	// Update the game logic
	virtual void OnUpdate(UpdateEventArgs& e);

	// Render stuff
	virtual void OnRender(RenderEventArgs& e);

	// Invoked by the registered window when a key is pressed while the window has focus
	virtual void OnKeyPressed(KeyEventArgs& e);

	// Invoked when a key on the keyboard is released
	virtual void OnKeyReleased(KeyEventArgs& e);

	// Invoked when the mouse is moved over the registered window
	virtual void OnMouseMoved(MouseMotionEventArgs& e);

	// Invoked when a mouse button is pressed over the registered window
	virtual void OnMouseButtonPressed(MouseButtonEventArgs& e);

	// Invoked when a mouse button is released over the registered window
	virtual void OnMouseButtonReleased(MouseButtonEventArgs& e);

	// Invoked when the mouse wheel is scrolled while the registered window has focus
	virtual void OnMouseWheel(MouseWheelEventArgs& e);

	// Invoked when the attached window is resized
	virtual void OnResize(ResizeEventArgs& e);

	// Invoked when the registered window instance is destroyed
	virtual void OnWindowDestroy();

private:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};
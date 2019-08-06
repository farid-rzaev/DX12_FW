#include "Game.h"


int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	const wchar_t* windowTitle = L"Learning DirectX 12";

	Game game (hInstance, windowTitle, 3500, 1800, false);
	//game.LoadContent();		// UNCOMMENT ME
	game.Run();
	game.UnloadContent();

	return 0;
}
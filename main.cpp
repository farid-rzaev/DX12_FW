#include "Application.h"


int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	const wchar_t* windowTitle = L"Learning DirectX 12";

	Application app;
	app.Init(hInstance, windowTitle);
	app.Run();
	app.Finish();

	return 0;
}
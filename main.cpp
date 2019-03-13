#include "Application.h"


int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	const wchar_t* windowTitle = L"Learning DirectX 12";

	Application::Create();
	{
		Application::Get().Init(hInstance, windowTitle);
		Application::Get().Run();
		Application::Get().Finish();
	}
	Application::Destroy();

	return 0;
}
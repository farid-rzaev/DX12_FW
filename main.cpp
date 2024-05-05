#include "Samples/00_Cube/Sample0.h"


int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	const wchar_t* windowTitle = L"Learning DirectX 12";

	Sample0::Create(hInstance, windowTitle, 1280, 720, false);
	{
		Sample0::Get().Run();
	}
	Sample0::Destroy();

	return 0;
}
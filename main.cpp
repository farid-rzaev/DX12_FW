#include "Game.h"


std::wstring GetExePath() 
{
	WCHAR path[MAX_PATH];
	HMODULE hModule = GetModuleHandleW(NULL);
	if (GetModuleFileNameW(hModule, path, MAX_PATH) > 0)
	{
		std::wstring wstr = std::wstring(path);
		size_t pos = wstr.rfind('\\');
		if (pos != std::string::npos) {
			auto str = wstr.substr(0, pos + 1);
			return str;
		}
	}
	return std::wstring(L"");
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	std::wstring exePath = GetExePath();
	assert(exePath.size() != 0);

	const wchar_t* windowTitle = L"Learning DirectX 12";

	Game game (hInstance, windowTitle, 3500, 1800, false);
	game.LoadContent(exePath);
	game.Run();
	game.UnloadContent();

	return 0;
}
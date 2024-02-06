#include "Samples/00_Cube/Sample0.h"


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

	Sample0 sample (hInstance, windowTitle, 1900, 900, false);
	sample.LoadContent(exePath);
	sample.Run();
	sample.UnloadContent();

	return 0;
}
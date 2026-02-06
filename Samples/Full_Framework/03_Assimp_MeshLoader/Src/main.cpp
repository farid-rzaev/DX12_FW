// This is the exact copy of the 00_Cube project but hooked up to the FULL (not simplified) 
// version of the framework DX12_FW.

#include "Sample3.h"

#include <dxgidebug.h>

void ReportLiveObjects()
{
	IDXGIDebug1* dxgiDebug;
	DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));

	dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
	dxgiDebug->Release();
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	const wchar_t* windowTitle = L"Sample 3 - ASSIMP - Mesh Loading";

	int retCode = 0;

	Application::Create(hInstance);
	{
		std::shared_ptr<Sample3> sample = std::make_shared<Sample3>();
		sample->Initialize(windowTitle, 1280, 720, false);

		sample->Run();
	}
	Application::Destroy();

	// ReportLiveObjects
	atexit(&ReportLiveObjects);

	return retCode;
}
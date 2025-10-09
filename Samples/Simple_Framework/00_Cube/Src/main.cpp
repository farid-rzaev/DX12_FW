#include "Sample0.h"

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
	const wchar_t* windowTitle = L"Learning DirectX 12";
	int retCode = 0;

	Sample0 sample(hInstance);
	sample.Initialize(windowTitle, 1280, 720, false);
	{
		retCode = sample.Run();
	}

	// ReportLiveObjects
	atexit(&ReportLiveObjects);

	return retCode;
}
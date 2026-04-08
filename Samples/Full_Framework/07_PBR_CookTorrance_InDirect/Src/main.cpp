#include "Sample7.h"

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
	const wchar_t* windowTitle = L"Sample 7 - PBR_CookTorrance Direct + Indirect.";

	int retCode = 0;

	Application::Create(hInstance);
	{
		std::shared_ptr<Sample7> sample = std::make_shared<Sample7>();
		sample->Initialize(windowTitle, 1280, 720, false);

		sample->Run();
	}
	Application::Destroy();

	// ReportLiveObjects
	atexit(&ReportLiveObjects);

	return retCode;
}
#include "PixProfiler.h"

#include <d3d12.h>   // ID3D12GraphicsCommandList

#define USE_PIX
#include <pix3.h>

#include <string>
#include <assert.h>
#include <libloaderapi.h>


#include <Framework/3RD_Party/Helpers.h>
#include <Framework/Application.h>

size_t PixProfiler::s_capture_cnt = 0;

PixProfiler::PixProfiler()
{
#if defined (_DEBUG)

	// GPU Capturer
	{
		// Load gpu capurer from PIX installation path
		m_gpu_capturer = PIXLoadLatestWinPixGpuCapturerLibrary();

		// If not found, load from current directory
		if (m_gpu_capturer == 0)
		{
			m_gpu_capturer = LoadLibraryA("WinPixGpuCapturer.dll");
		}

		if (m_gpu_capturer == 0)
		{
			OutputDebugStringW(L"Failed to load WinPixGpuCapturer.dll. Please ensure that it is either in the current directory or installed with PIX and that pix3.h matches the version of WinPixGpuCapturer.dll.");
		}
	}

	// Event Runtime
	#if 0 // If PIX_APP is not installed
	{
		HMODULE m_event_runtime = GetModuleHandleA("WinPixEventRuntime.dll");
		if (m_event_runtime == 0)
		{
			m_event_runtime = LoadLibraryA("WinPixEventRuntime.dll");
			assert(m_event_runtime && "Failed to load WinPixEventRuntime.dll");
		}
	}
	#endif

	if (s_enable_pix_hud)
	{
		PIXSetHUDOptions(PIX_HUD_SHOW_ON_ALL_WINDOWS);
	}
	else
	{
		PIXSetHUDOptions(PIX_HUD_SHOW_ON_NO_WINDOWS);
	}

#endif
}

PixProfiler::~PixProfiler()
{
	if (m_begin_capturing)
	{
		OutputDebugStringW(L"Capture session still active. Please call EndCapture before destroying the PixGpuCapturer.");
		EndGpuTraceCapture();
	}

	if (m_gpu_capturer)
	{
		FreeLibrary(m_gpu_capturer);
		m_gpu_capturer = nullptr;
	}

	#if 0 // If PIX_APP is not installed
	if (m_event_runtime)
	{
		FreeLibrary(m_event_runtime);
		m_event_runtime = 0;
	}
	#endif
}

bool PixProfiler::Loaded() const
{
	return m_gpu_capturer != 0;
}


void PixProfiler::BeginGpuTraceCapture()
{
	if (!Loaded())
	{
		return;
	}

	if (m_begin_capturing)
	{
		assert(false && "Already in a capture session. Please call EndCapture before starting a new capture.");
		return;
	}

	auto genOutputFileName = [](const std::wstring& prefix) -> std::wstring
		{
			std::wstring outputName = prefix;
			outputName += L"_f" + std::to_wstring(Application::Get().GetFrameCount());
			outputName += L"_t" + std::to_wstring(GetTickCount64());
			outputName += L"_#" + std::to_wstring(s_capture_cnt++);
			outputName += L".wpix";
			return outputName;
		};

	DWORD captureFlags = PIX_CAPTURE_GPU;
	PIXCaptureParameters captureParameters = {};

	std::wstring output_file_path = GetExePath();

	if (captureFlags & PIX_CAPTURE_TIMING)
	{
		// For timing captures, we want to capture callstacks to get a better view of where GPU time is being spent.
		captureParameters.TimingCaptureParameters.CaptureCallstacks = TRUE;

		output_file_path += genOutputFileName(L"PIX_Timing_Capture");
		captureParameters.TimingCaptureParameters.FileName = output_file_path.c_str();

	}
	else if (captureFlags & PIX_CAPTURE_GPU)
	{
		output_file_path += genOutputFileName(L"PIX_GPU_Capture");
		captureParameters.GpuCaptureParameters.FileName = output_file_path.c_str();
	}

	HRESULT hr = PIXBeginCapture(captureFlags, &captureParameters);
	if (FAILED(hr))
	{
		assert(false && "Failed to begin PIX GPU capture.");
		return; // m_begin_capturing = false;
	}

	m_begin_capturing = true;
}

void PixProfiler::EndGpuTraceCapture()
{
	if (!Loaded())
	{
		return;
	}

	if (!m_begin_capturing)
	{
		assert(false && "Not currently in a capture session. Please call BeginCapture before calling EndCapture.");
		return;
	}

	BOOL discard = FALSE;
	HRESULT hr = PIXEndCapture(discard);
	if (FAILED(hr))
	{
		assert(false && "Failed to end PIX GPU capture.");
	}

	m_begin_capturing = false;
}

void PixProfiler::PushMarker(void* commandList, const wchar_t* name)
{
	if (!Loaded() || commandList == nullptr)
	{
		return;
	}

	// Green color for GPU events
	const UINT8 r = 0, g = 255, b = 0; 

	PIXBeginEvent((ID3D12GraphicsCommandList*)commandList, PIX_COLOR(r, g, b), name);
}

void PixProfiler::PopMarker(void* commandList)
{
	if (!Loaded() || commandList == nullptr)
	{
		return;
	}

	PIXEndEvent((ID3D12GraphicsCommandList*) commandList);
}
#pragma once

#include <Framework/3RD_Party/Defines.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// ------------------------------------------------------------------------------------------
//									Basic Profiler
// ------------------------------------------------------------------------------------------

class DX12_FW_API PixProfiler
{
public:
	PixProfiler();
	~PixProfiler();
	
	bool Loaded() const;

	// GPU trace
	void BeginGpuTraceCapture();
	void EndGpuTraceCapture();

	// Perf Markers
	void PushMarker(void* commandList, const wchar_t* name);
	void PopMarker(void* commandList);

private:
	HMODULE m_gpu_capturer		= nullptr;
	bool    m_begin_capturing	= false;

	static const bool	s_enable_pix_hud = true;
	static size_t		s_capture_cnt;
};

// ------------------------------------------------------------------------------------------
//									Scoped Profiler
// ------------------------------------------------------------------------------------------

class ScopedMarker
{
public:
    ScopedMarker(PixProfiler& profiler, void* commandList, const wchar_t* name)
        : m_profiler(profiler), m_commandList(commandList)
    {
        m_profiler.PushMarker(m_commandList, name);
    }
    ~ScopedMarker()
    {
        m_profiler.PopMarker(m_commandList);
    }

    ScopedMarker(const ScopedMarker&) = delete;
    ScopedMarker& operator=(const ScopedMarker&) = delete;

private:
    PixProfiler& m_profiler;
    void* m_commandList = nullptr;
};

// ------------------------------------------------------------------------------------------
//									     MACROs
// ------------------------------------------------------------------------------------------

#define PIX_CONCAT(a, b) a##b
#define PIX_SCOPE_MARKER(profiler, cmdList, name) \
    ScopedMarker PIX_CONCAT(_pixScopeMarker_, __LINE__)((profiler), (cmdList), (name))

#define PIX_BEGIN_GPU_CAPTURE(profiler, capture) \
	if(capture == true) { profiler.BeginGpuTraceCapture(); }

#define PIX_END_GPU_CAPTURE(profiler, capture) \
	if(capture == true) { profiler.EndGpuTraceCapture(); }
#pragma once

// Minimize the num of
// headers from Windows.h
#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // For HRESULT
#include <exception> // For std::exception

#include <string>

// From DXSampleHelper.h 
// Source: https://github.com/Microsoft/DirectX-Graphics-Samples
inline void ThrowIfFailed(HRESULT hr, char const* const message = "")
{
	if (FAILED(hr))
	{
		throw std::exception(message);
	}
}

inline void ThrowIfFailed(bool success, char const* const message = "")
{
	if (!success)
	{
		throw std::exception(message);
	}
}

inline std::wstring GetExePath()
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
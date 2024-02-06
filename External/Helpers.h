#pragma once

// Minimize the num of
// headers from Windows.h
#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // For HRESULT
#include <exception> // For std::exception


// From DXSampleHelper.h 
// Source: https://github.com/Microsoft/DirectX-Graphics-Samples
inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw std::exception();
	}
}
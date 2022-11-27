#pragma once

#include <windows.h>
#include "viewer plugins.h"

// Define the exported DLL functions
extern "C"
{
	__declspec(dllexport) BOOL DVP_IdentifyW(LPVIEWERPLUGININFO lpVPInfo);
	__declspec(dllexport) BOOL DVP_IdentifyFileW(HWND hWnd, LPWSTR lpszName, LPVIEWERPLUGINFILEINFO lpVPFileInfo, HANDLE hAbortEvent);
	__declspec(dllexport) HBITMAP DVP_LoadBitmapW(HWND hWnd, LPWSTR lpszName, LPVIEWERPLUGINFILEINFO lpVPFileInfo, LPSIZE lpszDesiredSize, HANDLE hAbortEvent);
};

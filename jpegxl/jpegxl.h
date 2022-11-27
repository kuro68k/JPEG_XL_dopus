#pragma once

#include <windows.h>
#include "viewer plugins.h"

// Define the exported DLL functions
extern "C"
{
	//__declspec(dllexport) BOOL DVP_InitEx(LPDVPINITEXDATA pInitExData);
	//__declspec(dllexport) void DVP_Uninit(void);
	//__declspec(dllexport) BOOL DVP_USBSafe(LPOPUSUSBSAFEDATA pUSBSafeData);

	__declspec(dllexport) BOOL DVP_IdentifyW(LPVIEWERPLUGININFO lpVPInfo);
	__declspec(dllexport) BOOL DVP_IdentifyFileW(HWND hWnd, LPTSTR lpszName, LPVIEWERPLUGINFILEINFO lpVPFileInfo, HANDLE hAbortEvent);
	__declspec(dllexport) HBITMAP DVP_LoadBitmapW(HWND hWnd, LPTSTR lpszName, LPVIEWERPLUGINFILEINFO lpVPFileInfo, LPSIZE lpszDesiredSize, HANDLE hAbortEvent);
};

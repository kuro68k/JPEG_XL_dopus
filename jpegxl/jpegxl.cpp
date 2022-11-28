/* JPEG XL viewer plugin for Directory Opus 12
* 
* (C) 2022 Kuro68k and others (see LICENCE-libjxl)
*/

#include <shlwapi.h>
#include <Shlobj.h>

#include <iostream>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tchar.h>

#include <vector>
#include <string>

#include "jxl/decode.h"
#include "jxl/decode_cxx.h"
#include "jxl/resizable_parallel_runner.h"
#include "jxl/resizable_parallel_runner_cxx.h"

#include <windows.h>
#include <delayimp.h>

#include "jpegxl.h"

// {FA959248-AB91-4363-88B8-059C5D848824}
static const GUID GUIDPlugin_jpegxl =
{ 0xfa959248, 0xab91, 0x4363, { 0x88, 0xb8, 0x5, 0x9c, 0x5d, 0x84, 0x88, 0x24 } };

static HMODULE s_hDllModule = NULL;
static HINSTANCE hJXLDLL = NULL;

FARPROC WINAPI DelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	//if the failure was failure to load the designated dll
	if (dliNotify == dliFailLoadLib &&
		pdli->dwLastError == ERROR_MOD_NOT_FOUND)
	{
		// try the other path were the DLL is likely to be
		HMODULE lib = LoadLibrary(_T("Viewers\\libjxl.lld"));
		return (FARPROC)lib;
	}
	return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
		case DLL_PROCESS_ATTACH:
			s_hDllModule = reinterpret_cast<HMODULE>(hModule);
			break;
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}

bool LoadFile(LPTSTR filename, std::vector<uint8_t>* out) {
	FILE* file = _wfopen(filename, _T("rb"));
	if (!file) {
		return false;
	}

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return false;
	}

	long size = ftell(file);
	// Avoid invalid file or directory.
	if (size >= LONG_MAX || size < 0) {
		fclose(file);
		return false;
	}

	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return false;
	}

	out->resize(size);
	size_t readsize = fread(out->data(), 1, size, file);
	if (fclose(file) != 0) {
		return false;
	}

	return readsize == static_cast<size_t>(size);
}

// decode JPEG XL file
bool DecodeJpegXlOneShot(const uint8_t* jxl, size_t size,
	std::vector<uint32_t>* pixels, size_t* xsize,
	size_t* ysize, std::vector<uint8_t>* icc_profile)
{
	// Multi-threaded parallel runner.
	auto runner = JxlResizableParallelRunnerMake(nullptr);
	
	auto dec = JxlDecoderMake(nullptr);
	if (pixels != NULL)
	{
		if (JXL_DEC_SUCCESS !=
			JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO |
				JXL_DEC_COLOR_ENCODING |
				JXL_DEC_FULL_IMAGE))
		{
			fprintf(stderr, "JxlDecoderSubscribeEvents failed\n");
			return false;
		}
	}
	else	// just want the basic image info
	{
		if (JXL_DEC_SUCCESS !=
			JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO))
		{
			fprintf(stderr, "JxlDecoderSubscribeEvents failed\n");
			return false;
		}
	}
	
	if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(),
		JxlResizableParallelRunner,
		runner.get())) {
		fprintf(stderr, "JxlDecoderSetParallelRunner failed\n");
		return false;
	}
	
	JxlBasicInfo info;
	JxlPixelFormat format = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };

	JxlDecoderSetInput(dec.get(), jxl, size);
	JxlDecoderCloseInput(dec.get());

	for (;;) {
		JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

		if (status == JXL_DEC_ERROR) {
			fprintf(stderr, "Decoder error\n");
			return false;
		}
		else if (status == JXL_DEC_NEED_MORE_INPUT) {
			fprintf(stderr, "Error, already provided all input\n");
			return false;
		}
		else if (status == JXL_DEC_BASIC_INFO) {
			if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info)) {
				fprintf(stderr, "JxlDecoderGetBasicInfo failed\n");
				return false;
			}
			*xsize = info.xsize;
			*ysize = info.ysize;
			JxlResizableParallelRunnerSetThreads(
				runner.get(),
				JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
		}
		else if (status == JXL_DEC_COLOR_ENCODING) {
			// Get the ICC color profile of the pixel data
			size_t icc_size;
			if (JXL_DEC_SUCCESS !=
				JxlDecoderGetICCProfileSize(
					dec.get(), &format, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size)) {
				fprintf(stderr, "JxlDecoderGetICCProfileSize failed\n");
				return false;
			}
			icc_profile->resize(icc_size);
			if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(
				dec.get(), &format,
				JXL_COLOR_PROFILE_TARGET_DATA,
				icc_profile->data(), icc_profile->size())) {
				fprintf(stderr, "JxlDecoderGetColorAsICCProfile failed\n");
				return false;
			}
		}
		else if ((status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) && (pixels != NULL))
		{
			size_t buffer_size;
			if (JXL_DEC_SUCCESS !=
				JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size)) {
				fprintf(stderr, "JxlDecoderImageOutBufferSize failed\n");
				return false;
			}
			if (buffer_size != *xsize * *ysize * 4) {
				fprintf(stderr, "Invalid out buffer size %" PRIu64 " %" PRIu64 "\n",
					static_cast<uint64_t>(buffer_size),
					static_cast<uint64_t>(*xsize * *ysize * 16));
				return false;
			}
			pixels->resize(*xsize * *ysize * 4);
			void* pixels_buffer = (void*)pixels->data();
			size_t pixels_buffer_size = pixels->size() * sizeof(float);
			if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec.get(), &format,
				pixels_buffer,
				pixels_buffer_size)) {
				fprintf(stderr, "JxlDecoderSetImageOutBuffer failed\n");
				return false;
			}
		}
		else if (status == JXL_DEC_FULL_IMAGE) {
			// Nothing to do. Do not yet return. If the image is an animation, more
			// full frames may be decoded. This example only keeps the last one.
		}
		else if (status == JXL_DEC_SUCCESS) {
			// All decoding successfully finished.
			// It's not required to call JxlDecoderReleaseInput(dec.get()) here since
			// the decoder will be destroyed.
			return true;
		}
		else {
			fprintf(stderr, "Unknown decoder status\n");
			return false;
		}
	}
}

// Identify the viewer plugin to DOpus
BOOL DVP_IdentifyW(LPVIEWERPLUGININFO lpVPInfo)
{
	if (lpVPInfo->cbSize >= sizeof(VIEWERPLUGININFO))
	{
		lpVPInfo->dwFlags = DVPFIF_ExtensionsOnly |
							DVPFIF_NeedRandomSeek |
							DVPFIF_NoMultithreadThumbnails;

		// Version number (H.L.H.L)
		lpVPInfo->dwVersionHigh = MAKELPARAM(0, 0);
		lpVPInfo->dwVersionLow = MAKELPARAM(2, 0);

		(void)lstrcpyn(lpVPInfo->lpszHandleExts, _T(".jxl"), lpVPInfo->cchHandleExtsMax);
		(void)lstrcpyn(lpVPInfo->lpszName, _T("JPEG XL"), lpVPInfo->cchNameMax);
		(void)lstrcpyn(lpVPInfo->lpszDescription, TEXT("JPEG XL Viewer Plugin"), lpVPInfo->cchDescriptionMax);
		(void)lstrcpyn(lpVPInfo->lpszCopyright, TEXT("(c) Copyright 2022 Kuro68k"), lpVPInfo->cchCopyrightMax);

		lpVPInfo->dwlMinFileSize = 100;
		lpVPInfo->uiMajorFileType = DVPMajorType_Image;
		lpVPInfo->idPlugin = GUIDPlugin_jpegxl;

		return TRUE;
	}
	return FALSE;
}

// Identify a local disk-based file
BOOL DVP_IdentifyFileW(HWND hWnd, LPWSTR lpszName, LPVIEWERPLUGINFILEINFO lpVPFileInfo, HANDLE hAbortEvent)
{
	std::vector<uint8_t> jxl;
	if (!LoadFile(lpszName, &jxl)) {
		fwprintf(stderr, _T("couldn't load %s\n"), lpszName);
		return FALSE;
	}

	size_t xsize = 0, ysize = 0;
	if (!DecodeJpegXlOneShot(jxl.data(), jxl.size(), NULL, &xsize, &ysize, NULL)) {
		fwprintf(stderr, _T("Error while decoding the jxl file\n"));
		return 1;
	}

	// Fill out file information and return success
	lpVPFileInfo->dwFlags = DVPFIF_CanReturnBitmap | DVPFIF_CanReturnViewer | DVPFIF_CanReturnThumbnail;
	lpVPFileInfo->wMajorType = DVPMajorType_Image;
	lpVPFileInfo->wMinorType = 0;
	lpVPFileInfo->szImageSize.cx = (LONG)xsize;
	lpVPFileInfo->szImageSize.cy = (LONG)ysize;
	lpVPFileInfo->iNumBits = 32;
	if (lpVPFileInfo->lpszInfo)
		wsprintf(lpVPFileInfo->lpszInfo, TEXT("%ld x %ld JPEG XL Image"), lpVPFileInfo->szImageSize.cx, lpVPFileInfo->szImageSize.cy);
	return TRUE;
}

// Create a bitmap from a disk-based TGA file
HBITMAP DVP_LoadBitmapW(HWND hWnd, LPWSTR lpszName, LPVIEWERPLUGINFILEINFO lpVPFileInfo, LPSIZE lpszDesiredSize, HANDLE hAbortEvent)
{
	std::vector<uint8_t> jxl;
	if (!LoadFile(lpszName, &jxl)) {
		fwprintf(stderr, _T("couldn't load %s\n"), lpszName);
		return NULL;
	}

	std::vector<uint32_t> pixels;
	std::vector<uint8_t> icc_profile;
	size_t xsize = 0, ysize = 0;
	if (!DecodeJpegXlOneShot(jxl.data(), jxl.size(), &pixels, &xsize, &ysize, &icc_profile))
	{
		fwprintf(stderr, _T("Error while decoding %s\n"), lpszName);
		return NULL;
	}

	HBITMAP hBitmap = NULL;
	BITMAPINFOHEADER bmih;
	bmih.biSize = sizeof(BITMAPINFOHEADER);
	bmih.biWidth = (long)xsize;
	bmih.biHeight = (long)(0 - ysize);
	bmih.biPlanes = 1;
	bmih.biBitCount = 32;
	bmih.biCompression = BI_RGB;
	bmih.biSizeImage = 0;
	bmih.biXPelsPerMeter = 10;
	bmih.biYPelsPerMeter = 10;
	bmih.biClrUsed = 0;
	bmih.biClrImportant = 0;

	BITMAPINFO dbmi;
	ZeroMemory(&dbmi, sizeof(dbmi));
	dbmi.bmiHeader = bmih;
	dbmi.bmiColors->rgbBlue = 0;
	dbmi.bmiColors->rgbGreen = 0;
	dbmi.bmiColors->rgbRed = 0;
	dbmi.bmiColors->rgbReserved = 0;
	void* bits = (void*)&(pixels);

	// convert RGBA to BGRA
	size_t count = xsize * ysize;
	uint8_t* p = (uint8_t*)pixels.data();
	for (size_t i = 0; i < count; i++)
	{
		uint8_t r = *p;
		uint8_t b = *(p+2);
		*p = b;
		*(p + 2) = r;
		p += 4;
	}

	// create DIB
	HDC hdc = ::GetDC(NULL);
	hBitmap = CreateDIBitmap(hdc, &bmih, CBM_INIT, pixels.data(), &dbmi, DIB_RGB_COLORS);
	if (hBitmap == NULL) {
		fwprintf(stderr, _T("Error while creating DIB section\n"));
		return NULL;
	}
	::ReleaseDC(NULL, hdc);

	return hBitmap;
}

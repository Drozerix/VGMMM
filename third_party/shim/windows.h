// Minimal stand-in for <windows.h> so the bundled emulator sources (misc/*_src) build on Linux.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef __declspec
#define __declspec(x)
#endif
#define __stdcall
#define __cdecl
#define WINAPI
typedef int BOOL;
typedef uint32_t DWORD;
typedef void *HANDLE, *HINSTANCE, *LPVOID, *HWND;
typedef uint8_t BYTE;
typedef uint16_t WORD;
#define TRUE 1
#define FALSE 0
static inline DWORD GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
// the VGM optimiser reports its progress on the console; the editor has none
#define printf(...) ((void)0)

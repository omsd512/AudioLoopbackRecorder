#pragma once
#include <cstdint>

#ifdef BUILDING_AUDIO_CAPTURE_DLL
#define ACDLL_API __declspec(dllexport)
#else
#define ACDLL_API __declspec(dllimport)
#endif

extern "C" {

typedef void (CALLBACK *pcm_callback_t)(const uint8_t* data, int bytes, void* user_data);

ACDLL_API bool __stdcall StartCaptureForPid(unsigned int pid,
                                            bool includeTree,
                                            pcm_callback_t cb,
                                            void* user_data,
                                            unsigned int requestedSampleRate);

ACDLL_API void __stdcall StopCapture();

ACDLL_API int __stdcall GetCaptureFormat(unsigned int* sampleRate, unsigned short* channels, unsigned short* bitsPerSample);
}
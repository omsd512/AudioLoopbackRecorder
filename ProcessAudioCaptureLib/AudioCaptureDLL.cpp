#include "AudioCaptureDLL.h"
#include "../source/LoopbackCapture.h"
#include <mutex>
#include <memory>

static std::mutex g_managerMutex;
static std::unique_ptr<CLoopbackCapture> g_capture;

extern "C" {

ACDLL_API bool __stdcall StartCaptureForPid(unsigned int pid,
                                            bool includeTree,
                                            pcm_callback_t cb,
                                            void* user_data,
                                            unsigned int requestedSampleRate)
{
    std::lock_guard<std::mutex> lock(g_managerMutex);
    if (g_capture) return false;
    g_capture = std::make_unique<CLoopbackCapture>();
    HRESULT hr = g_capture->StartCaptureWithCallback((DWORD)pid, includeTree, cb, user_data, requestedSampleRate);
    if (FAILED(hr)) { g_capture.reset(); return false; }
    return true;
}

ACDLL_API void __stdcall StopCapture()
{
    std::lock_guard<std::mutex> lock(g_managerMutex);
    if (g_capture) { g_capture->StopCaptureAsync(); g_capture.reset(); }
}

ACDLL_API int __stdcall GetCaptureFormat(unsigned int* sampleRate, unsigned short* channels, unsigned short* bitsPerSample)
{
    std::lock_guard<std::mutex> lock(g_managerMutex);
    if (!g_capture) return 0;
    UINT32 sr=0; UINT16 ch=0; UINT16 bits=0;
    g_capture->GetCurrentFormat(&sr, &ch, &bits);
    if (sampleRate) *sampleRate = (unsigned int)sr;
    if (channels) *channels = ch;
    if (bitsPerSample) *bitsPerSample = bits;
    return 1;
}

}
#pragma once

/*
 Combined original LoopbackCapture.h with callback additions
*/

#include <AudioClient.h>
#include <mmdeviceapi.h>
#include <initguid.h>
#include <guiddef.h>
#include <mfapi.h>

#include <wrl\implements.h>
#include <wil\com.h>
#include <wil\result.h>

#include "Common.h"

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

using namespace Microsoft::WRL;

// 回调类型（__stdcall）
typedef void (CALLBACK *pcm_callback_t)(const uint8_t* data, int bytes, void* user_data);

class CLoopbackCapture :
 public RuntimeClass< RuntimeClassFlags< ClassicCom >, FtmBase, IActivateAudioInterfaceCompletionHandler >
{
public:
 CLoopbackCapture();
 ~CLoopbackCapture();

 HANDLE GetStopEventHandle() { return m_hCaptureStopped.get(); }

 HRESULT StartGlobalCaptureAsync(PCWSTR outputFileName);
 HRESULT StartCaptureAsync(DWORD processId, bool includeProcessTree, PCWSTR outputFileName);
 HRESULT StartCaptureWithCallback(DWORD processId, bool includeProcessTree, pcm_callback_t callback, void* user_data, UINT32 requestedSampleRate = 44100);

 HRESULT StopCaptureAsync();

 METHODASYNCCALLBACK(CLoopbackCapture, StartCapture, OnStartCapture);
 METHODASYNCCALLBACK(CLoopbackCapture, StopCapture, OnStopCapture);
 METHODASYNCCALLBACK(CLoopbackCapture, SampleReady, OnSampleReady);

 STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation);

private:
 enum class DeviceState { Uninitialized, Error, Initialized, Starting, Capturing, Stopping, Stopped };

 HRESULT OnStartCapture(IMFAsyncResult* pResult);
 HRESULT OnStopCapture(IMFAsyncResult* pResult);
 HRESULT OnSampleReady(IMFAsyncResult* pResult);

 HRESULT InitializeLoopbackCapture();
 HRESULT CreateWAVFile();
 HRESULT FixWAVHeader();
 HRESULT OnAudioSampleRequested();
 HRESULT ActivateAudioInterface(DWORD processId, bool includeProcessTree);
 HRESULT SetDeviceStateErrorIfFailed(HRESULT hr);
 HRESULT ActivateAudioInterfaceGlobal();
 void WriterThreadProc();

 // members
 wil::com_ptr_nothrow<IAudioClient> m_AudioClient;
 WAVEFORMATEX m_CaptureFormat{};
 UINT32 m_BufferFrames = 0;
 wil::com_ptr_nothrow<IAudioCaptureClient> m_AudioCaptureClient;
 wil::com_ptr_nothrow<IMFAsyncResult> m_SampleReadyAsyncResult;

 wil::unique_event_nothrow m_SampleReadyEvent;
 MFWORKITEM_KEY m_SampleReadyKey = 0;
 wil::unique_hfile m_hFile;
 wil::critical_section m_CritSec;
 DWORD m_dwQueueID = 0;
 DWORD m_cbHeaderSize = 0;
 DWORD m_cbDataSize = 0;

 PCWSTR m_outputFileName = nullptr;
 HRESULT m_activateResult = E_UNEXPECTED;

 DeviceState m_DeviceState{ DeviceState::Uninitialized };
 wil::unique_event_nothrow m_hActivateCompleted;
 wil::unique_event_nothrow m_hCaptureStopped;

 std::thread m_WriterThread;
 std::queue<std::vector<BYTE>> m_AudioQueue;
 std::mutex m_QueueMutex;
 std::condition_variable m_QueueCV;
 std::atomic<bool> m_bIsCapturing;
 std::atomic<HRESULT> m_writerThreadResult;

 // callback members
 pcm_callback_t m_userCallback = nullptr;
 void* m_userData = nullptr;
};

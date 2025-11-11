// Updated LoopbackCapture.cpp with callback-mode integration
#include <shlobj.h>
#include <wchar.h>
#include <iostream>
#include <audioclientactivationparams.h>

#include "LoopbackCapture.h"

#define BITS_PER_BYTE 8

CLoopbackCapture::CLoopbackCapture() :
 m_bIsCapturing(false),
 m_writerThreadResult(S_OK)
{
}

HRESULT CLoopbackCapture::SetDeviceStateErrorIfFailed(HRESULT hr)
{
 if (FAILED(hr))
 {
  m_DeviceState = DeviceState::Error;
 }
 return hr;
}

HRESULT CLoopbackCapture::InitializeLoopbackCapture()
{
 RETURN_IF_FAILED(m_SampleReadyEvent.create(wil::EventOptions::None));
 RETURN_IF_FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
 DWORD dwTaskID = 0;
 RETURN_IF_FAILED(MFLockSharedWorkQueue(L"Capture", 0, &dwTaskID, &m_dwQueueID));
 m_xSampleReady.SetQueueID(m_dwQueueID);
 RETURN_IF_FAILED(m_hActivateCompleted.create(wil::EventOptions::None));
 RETURN_IF_FAILED(m_hCaptureStopped.create(wil::EventOptions::None));
 return S_OK;
}

CLoopbackCapture::~CLoopbackCapture()
{
 if (m_WriterThread.joinable())
 {
  if (m_bIsCapturing)
  {
   StopCaptureAsync();
  }
  else
  {
   m_WriterThread.join();
  }
 }
 if (m_dwQueueID != 0)
 {
  MFUnlockWorkQueue(m_dwQueueID);
 }
}

HRESULT CLoopbackCapture::ActivateAudioInterface(DWORD processId, bool includeProcessTree)
{
 return SetDeviceStateErrorIfFailed([&]() -> HRESULT
 {
  AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
  audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
  audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = includeProcessTree ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
  audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = processId;

  PROPVARIANT activateParams = {};
  activateParams.vt = VT_BLOB;
  activateParams.blob.cbSize = sizeof(audioclientActivationParams);
  activateParams.blob.pBlobData = (BYTE*)&audioclientActivationParams;

  wil::com_ptr_nothrow<IActivateAudioInterfaceAsyncOperation> asyncOp;
  RETURN_IF_FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activateParams, this, &asyncOp));

  m_hActivateCompleted.wait();
  return m_activateResult;
 }());
}

HRESULT CLoopbackCapture::ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
{
 m_activateResult = SetDeviceStateErrorIfFailed([&]()->HRESULT
 {
  HRESULT hrActivateResult = E_UNEXPECTED;
  wil::com_ptr_nothrow<IUnknown> punkAudioInterface;
  RETURN_IF_FAILED(operation->GetActivateResult(&hrActivateResult, &punkAudioInterface));
  RETURN_IF_FAILED(hrActivateResult);
  RETURN_IF_FAILED(punkAudioInterface.copy_to(&m_AudioClient));

  // capture format
  m_CaptureFormat.wFormatTag = WAVE_FORMAT_PCM;
  m_CaptureFormat.nChannels = 2;
  m_CaptureFormat.nSamplesPerSec = 44100;
  m_CaptureFormat.wBitsPerSample = 16;
  m_CaptureFormat.nBlockAlign = m_CaptureFormat.nChannels * m_CaptureFormat.wBitsPerSample / BITS_PER_BYTE;
  m_CaptureFormat.nAvgBytesPerSec = m_CaptureFormat.nSamplesPerSec * m_CaptureFormat.nBlockAlign;

  // initialize audio client - use event callback and loopback
  RETURN_IF_FAILED(m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
    200000,
    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
    &m_CaptureFormat,
    nullptr));

  RETURN_IF_FAILED(m_AudioClient->GetBufferSize(&m_BufferFrames));
  RETURN_IF_FAILED(m_AudioClient->GetService(IID_PPV_ARGS(&m_AudioCaptureClient)));
  RETURN_IF_FAILED(MFCreateAsyncResult(nullptr, &m_xSampleReady, nullptr, &m_SampleReadyAsyncResult));
  RETURN_IF_FAILED(m_AudioClient->SetEventHandle(m_SampleReadyEvent.get()));

  // Only create WAV file if not in callback mode
  if (m_userCallback == nullptr)
  {
    RETURN_IF_FAILED(CreateWAVFile());
  }

  m_DeviceState = DeviceState::Initialized;
  return S_OK;
 }());
 m_hActivateCompleted.SetEvent();
 return S_OK;
}

HRESULT CLoopbackCapture::CreateWAVFile()
{
 return SetDeviceStateErrorIfFailed([&]()->HRESULT
 {
  m_hFile.reset(CreateFile(m_outputFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
  RETURN_LAST_ERROR_IF(!m_hFile);

  DWORD header[] = { FCC('RIFF'), 0, FCC('WAVE'), FCC('fmt '), sizeof(m_CaptureFormat) };
  DWORD dwBytesWritten = 0;
  RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), header, sizeof(header), &dwBytesWritten, NULL));
  m_cbHeaderSize += dwBytesWritten;

  WI_ASSERT(m_CaptureFormat.cbSize == 0);
  RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &m_CaptureFormat, sizeof(m_CaptureFormat), &dwBytesWritten, NULL));
  m_cbHeaderSize += dwBytesWritten;

  DWORD data[] = { FCC('data'), 0 };
  RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), data, sizeof(data), &dwBytesWritten, NULL));
  m_cbHeaderSize += dwBytesWritten;

  return S_OK;
 }());
}

HRESULT CLoopbackCapture::FixWAVHeader()
{
 DWORD dwPtr = SetFilePointer(m_hFile.get(), m_cbHeaderSize - sizeof(DWORD), NULL, FILE_BEGIN);
 RETURN_LAST_ERROR_IF(INVALID_SET_FILE_POINTER == dwPtr);
 DWORD dwBytesWritten = 0;
 RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &m_cbDataSize, sizeof(DWORD), &dwBytesWritten, NULL));
 RETURN_LAST_ERROR_IF(INVALID_SET_FILE_POINTER == SetFilePointer(m_hFile.get(), sizeof(DWORD), NULL, FILE_BEGIN));
 DWORD cbTotalSize = m_cbDataSize + m_cbHeaderSize - 8;
 RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &cbTotalSize, sizeof(DWORD), &dwBytesWritten, NULL));
 RETURN_IF_WIN32_BOOL_FALSE(FlushFileBuffers(m_hFile.get()));
 return S_OK;
}

HRESULT CLoopbackCapture::StartCaptureAsync(DWORD processId, bool includeProcessTree, PCWSTR outputFileName)
{
 m_outputFileName = outputFileName;
 auto resetOutputFileName = wil::scope_exit([&] { m_outputFileName = nullptr; });
 RETURN_IF_FAILED(InitializeLoopbackCapture());
 RETURN_IF_FAILED(ActivateAudioInterface(processId, includeProcessTree));
 if (m_DeviceState == DeviceState::Initialized)
 {
  m_DeviceState = DeviceState::Starting;
  return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartCapture, nullptr);
 }
 return S_OK;
}

HRESULT CLoopbackCapture::StartGlobalCaptureAsync(PCWSTR outputFileName)
{
 m_outputFileName = outputFileName;
 auto resetOutputFileName = wil::scope_exit([&] { m_outputFileName = nullptr; });
 RETURN_IF_FAILED(InitializeLoopbackCapture());
 RETURN_IF_FAILED(ActivateAudioInterfaceGlobal());
 if (m_DeviceState == DeviceState::Initialized)
 {
  m_DeviceState = DeviceState::Starting;
  return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartCapture, nullptr);
 }
 return S_OK;
}

HRESULT CLoopbackCapture::ActivateAudioInterfaceGlobal()
{
 return SetDeviceStateErrorIfFailed([&]() -> HRESULT
 {
  wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
  wil::com_ptr_nothrow<IMMDevice> device;
  RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
  RETURN_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));
  RETURN_IF_FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_AudioClient));

  m_CaptureFormat.wFormatTag = WAVE_FORMAT_PCM;
  m_CaptureFormat.nChannels = 2;
  m_CaptureFormat.nSamplesPerSec = 44100;
  m_CaptureFormat.wBitsPerSample = 16;
  m_CaptureFormat.nBlockAlign = m_CaptureFormat.nChannels * m_CaptureFormat.wBitsPerSample / BITS_PER_BYTE;
  m_CaptureFormat.nAvgBytesPerSec = m_CaptureFormat.nSamplesPerSec * m_CaptureFormat.nBlockAlign;
  m_CaptureFormat.cbSize = 0;

  RETURN_IF_FAILED(m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
    200000,
    0,
    &m_CaptureFormat,
    nullptr));

  RETURN_IF_FAILED(m_AudioClient->GetBufferSize(&m_BufferFrames));
  RETURN_IF_FAILED(m_AudioClient->GetService(IID_PPV_ARGS(&m_AudioCaptureClient)));
  RETURN_IF_FAILED(MFCreateAsyncResult(nullptr, &m_xSampleReady, nullptr, &m_SampleReadyAsyncResult));
  RETURN_IF_FAILED(m_AudioClient->SetEventHandle(m_SampleReadyEvent.get()));

  if (m_userCallback == nullptr)
  {
    RETURN_IF_FAILED(CreateWAVFile());
  }

  m_DeviceState = DeviceState::Initialized;
  return S_OK;
 }());
}

HRESULT CLoopbackCapture::OnStartCapture(IMFAsyncResult* pResult)
{
 return SetDeviceStateErrorIfFailed([&]()->HRESULT
 {
  RETURN_IF_FAILED(m_AudioClient->Start());
  m_DeviceState = DeviceState::Capturing;

  if (m_userCallback == nullptr)
  {
    m_bIsCapturing = true;
    m_writerThreadResult = S_OK;
    m_WriterThread = std::thread(&CLoopbackCapture::WriterThreadProc, this);
  }
  else
  {
    m_bIsCapturing = true; // still mark capturing for sample loop
  }

  MFPutWaitingWorkItem(m_SampleReadyEvent.get(), 0, m_SampleReadyAsyncResult.get(), &m_SampleReadyKey);
  return S_OK;
 }());
}

HRESULT CLoopbackCapture::StopCaptureAsync()
{
 RETURN_HR_IF(E_NOT_VALID_STATE, (m_DeviceState != DeviceState::Capturing) && (m_DeviceState != DeviceState::Error));
 if (m_DeviceState == DeviceState::Stopping || m_DeviceState == DeviceState::Stopped)
 {
  return S_OK;
 }
 m_DeviceState = DeviceState::Stopping;
 RETURN_IF_FAILED(MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStopCapture, nullptr));
 m_hCaptureStopped.wait();
 if (m_WriterThread.joinable())
 {
  m_WriterThread.join();
 }
 m_DeviceState = DeviceState::Stopped;
 return m_writerThreadResult;
}

HRESULT CLoopbackCapture::OnStopCapture(IMFAsyncResult* pResult)
{
 if (0 != m_SampleReadyKey)
 {
  MFCancelWorkItem(m_SampleReadyKey);
  m_SampleReadyKey = 0;
 }
 m_AudioClient->Stop();
 m_SampleReadyAsyncResult.reset();
 m_bIsCapturing = false;
 m_QueueCV.notify_one();
 m_hCaptureStopped.SetEvent();
 return S_OK;
}

HRESULT CLoopbackCapture::OnSampleReady(IMFAsyncResult* pResult)
{
 if (SUCCEEDED(OnAudioSampleRequested()))
 {
  if (m_DeviceState == DeviceState::Capturing)
  {
    return MFPutWaitingWorkItem(m_SampleReadyEvent.get(), 0, m_SampleReadyAsyncResult.get(), &m_SampleReadyKey);
  }
 }
 else
 {
  m_DeviceState = DeviceState::Error;
 }
 return S_OK;
}

HRESULT CLoopbackCapture::OnAudioSampleRequested()
{
 UINT32 FramesAvailable = 0;
 BYTE* Data = nullptr;
 DWORD dwCaptureFlags;
 UINT64 u64DevicePosition = 0;
 UINT64 u64QPCPosition = 0;
 auto lock = m_CritSec.lock();
 if (m_DeviceState == DeviceState::Stopping || m_DeviceState == DeviceState::Stopped)
 {
  return S_OK;
 }
 while (SUCCEEDED(m_AudioCaptureClient->GetNextPacketSize(&FramesAvailable)) && FramesAvailable > 0)
 {
  UINT32 cbBytesToCapture = FramesAvailable * m_CaptureFormat.nBlockAlign;
  RETURN_IF_FAILED(m_AudioCaptureClient->GetBuffer(&Data, &FramesAvailable, &dwCaptureFlags, &u64DevicePosition, &u64QPCPosition));

  if (m_userCallback)
  {
    // call callback synchronously; callback MUST copy data before return
    m_userCallback((const uint8_t*)Data, (int)cbBytesToCapture, m_userData);
  }
  else
  {
    try
    {
      std::vector<BYTE> audioChunk(Data, Data + cbBytesToCapture);
      {
        std::lock_guard<std::mutex> queueLock(m_QueueMutex);
        m_AudioQueue.push(std::move(audioChunk));
      }
      m_QueueCV.notify_one();
    }
    catch (const std::bad_alloc&)
    {
      m_writerThreadResult = E_OUTOFMEMORY;
      StopCaptureAsync();
      break;
    }
  }

  m_AudioCaptureClient->ReleaseBuffer(FramesAvailable);
 }
 return S_OK;
}

void CLoopbackCapture::WriterThreadProc()
{
 while (m_bIsCapturing || !m_AudioQueue.empty())
 {
  std::vector<BYTE> audioData;
  {
    std::unique_lock<std::mutex> lock(m_QueueMutex);
    m_QueueCV.wait(lock, [this] { return !m_AudioQueue.empty() || !m_bIsCapturing; });
    if (m_AudioQueue.empty())
    {
      continue;
    }
    audioData = std::move(m_AudioQueue.front());
    m_AudioQueue.pop();
  }
  if (!audioData.empty())
  {
    DWORD dwBytesWritten = 0;
    if (!WriteFile(m_hFile.get(), audioData.data(), static_cast<DWORD>(audioData.size()), &dwBytesWritten, NULL))
    {
      m_writerThreadResult = HRESULT_FROM_WIN32(GetLastError());
      m_bIsCapturing = false;
      continue;
    }
    m_cbDataSize += dwBytesWritten;
  }
 }
 if (SUCCEEDED(m_writerThreadResult))
 {
  HRESULT hr = FixWAVHeader();
  if (FAILED(hr))
  {
    m_writerThreadResult = hr;
  }
 }
}

// New API: start capture with callback
HRESULT CLoopbackCapture::StartCaptureWithCallback(DWORD processId, bool includeProcessTree, pcm_callback_t callback, void* user_data, UINT32 /*requestedSampleRate*/)
{
 if (m_bIsCapturing.load()) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
 if (callback == nullptr) return E_INVALIDARG;
 m_userCallback = callback;
 m_userData = user_data;
 RETURN_IF_FAILED(InitializeLoopbackCapture());
 RETURN_IF_FAILED(ActivateAudioInterface(processId, includeProcessTree));
 if (m_DeviceState == DeviceState::Initialized)
 {
  m_DeviceState = DeviceState::Starting;
  return MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartCapture, nullptr);
 }
 return S_OK;
}

void CLoopbackCapture::GetCurrentFormat(UINT32* sampleRate, UINT16* channels, UINT16* bitsPerSample)
{
 if (sampleRate) *sampleRate = m_CaptureFormat.nSamplesPerSec;
 if (channels) *channels = m_CaptureFormat.nChannels;
 if (bitsPerSample) *bitsPerSample = m_CaptureFormat.wBitsPerSample;
}

// Keep existing placeholders for other interfaces if any

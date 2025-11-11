# Python usage example (ctypes callback). Edit DLL_PATH to your built DLL location.

import ctypes
import threading
import queue
import wave
import time
from ctypes import c_uint, c_bool, c_void_p, POINTER, c_ubyte, c_int, c_ushort
from ctypes import WINFUNCTYPE

DLL_PATH = r"C:\path\to\ProcessAudioCaptureLib.dll"  # <-- change this
dll = ctypes.WinDLL(DLL_PATH)

PCM_CALLBACK = WINFUNCTYPE(None, POINTER(c_ubyte), c_int, c_void_p)

dll.StartCaptureForPid.argtypes = (c_uint, c_bool, PCM_CALLBACK, c_void_p, c_uint)
dll.StartCaptureForPid.restype = c_bool
dll.StopCapture.argtypes = ()
dll.StopCapture.restype = None
dll.GetCaptureFormat.argtypes = (ctypes.POINTER(c_uint), ctypes.POINTER(c_ushort), ctypes.POINTER(c_ushort))
dll.GetCaptureFormat.restype = c_int

q = queue.Queue(maxsize=200)

@PCM_CALLBACK
def on_pcm(data_ptr, length, user):
    if length <= 0:
        return
    buf = ctypes.string_at(data_ptr, length)  # immediate copy
    try:
        q.put_nowait(buf)
    except queue.Full:
        pass

def writer(wav_path, sample_rate, channels, sampwidth):
    wf = wave.open(wav_path, 'wb')
    wf.setnchannels(channels)
    wf.setsampwidth(sampwidth)
    wf.setframerate(sample_rate)
    try:
        while True:
            data = q.get()
            if data is None:
                break
            wf.writeframes(data)
    finally:
        wf.close()

def main(pid):
    t = threading.Thread(target=writer, args=(f"out_pid_{pid}.wav", 44100, 2, 2), daemon=True)
    t.start()
    ok = dll.StartCaptureForPid(pid, True, on_pcm, None, 44100)
    if not ok:
        print("Start failed")
        return
    try:
        print("Capturing... Ctrl+C to stop")
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Stopping")
    finally:
        dll.StopCapture()
        q.put(None)
        t.join()

if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("usage: python python_example.py <PID>")
    else:
        main(int(sys.argv[1]))

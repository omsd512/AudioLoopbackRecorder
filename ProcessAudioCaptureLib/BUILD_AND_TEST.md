````markdown
name=ProcessAudioCaptureLib/BUILD_AND_TEST.md
```markdown
# ProcessAudioCaptureLib - Build & Test

This project adds a DLL wrapper for real-time process loopback audio capture.

## Prerequisites
- Windows 10/11
- Visual Studio 2022 with "Desktop development with C++" workload
- Windows 10 SDK
- NuGet restore for WIL (Microsoft.Windows.ImplementationLibrary) — restore via Visual Studio

## How to build
1. Open Visual Studio 2022.
2. Open the project file: `ProcessAudioCaptureLib/ProcessAudioCaptureLib.vcxproj`.
3. Restore NuGet packages (if required) for WIL.
4. Select `x64` platform and `Debug` or `Release` configuration.
5. Build the project.

## Output
- After successful build, `ProcessAudioCaptureLib.dll` will be in the project's output folder (e.g. `.`\x64\Release\`).

## Testing with Python example
- Use the provided `python_example.py` (adjust DLL_PATH).
- `python_example.py <PID>` will start capture and write `out_pid_<PID>.wav`.
- Ensure the target PID is playing audio.

Note: The LoopbackCapture implementation has been merged to add callback-mode. Ensure you test with a simple audio source first.
```

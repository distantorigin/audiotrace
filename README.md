# AudioTrace

AudioTrace shows which apps are making sound on Windows and macOS. It runs from the system tray or menu bar and keeps a local history of recent sound events.

## Download

[Download the latest release](https://github.com/distantorigin/audiotrace/releases/latest).

AudioTrace supports Windows 10 and 11. Download the x64 ZIP for most PCs or the ARM64 ZIP for Windows on Arm. Extract it and run `AudioTrace.exe`. It does not need installation or administrator access.

AudioTrace requires macOS 14.2 or later. Download the arm64 DMG for Apple silicon or the x86_64 DMG for an Intel Mac. Open the DMG and drag AudioTrace to Applications. If macOS blocks the first launch, open System Settings > Privacy & Security and click **Open Anyway**. Allow system audio recording when macOS asks.

## Use

Click the waveform icon to see the latest sound events. The menu shows up to ten; **Show all traces...** opens up to 500.

Each entry includes the time, app, process ID, peak level, and available source details. Continuous sound stays in one entry. A new sound after half a second of silence starts another.

The menu also lets you pause monitoring, copy the history, or clear it. Closing the history window does not stop monitoring.

## Privacy

AudioTrace does not save audio or make network requests. It keeps event metadata in memory until you clear it or quit. Copying the history writes that metadata to the clipboard.

## Build from source

Windows requires MinGW-w64 GCC and `windres` on `PATH`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

macOS builds require Xcode Command Line Tools:

```sh
bash macos/build.sh
```

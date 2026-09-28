# VR Piston Animation

Tiny OpenVR app that animates the chaperone working zero pose. It can run
from command-line values or be controlled by VRChat OSC.

It is intentionally conservative:

- It updates the OpenVR chaperone working copy while the program is running.
- It calls `ShowWorkingSetPreview()` so the working set is visible.
- It does not commit changes.
- Each time it is enabled, it reinitializes OpenVR and then uses the current
  working pose as its base. This refreshes the pose visible to this process so
  recent OVRAS offsets can be included.
- When disabled or exiting, it removes this app's current animation offset from
  the current working pose and shows the resulting working set preview.
- It does not call `RevertWorkingCopy()` at startup, because that would discard
  this process's current working copy.

OVRAS compatibility note: this app refreshes OpenVR on each enable because the
`IVRChaperoneSetup` state visible to this process can otherwise become stale
after OVRAS changes offsets.

## Build

This project supports Windows x64 builds only. CMake configuration on other
operating systems stops with an error.

If you have Visual Studio 2022 installed, use the Visual Studio generator from
PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

If you are inside a Visual Studio x64 Native Tools Command Prompt and CMake
selects `NMake Makefiles`, do not pass `-A x64`:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

If you already tried with the wrong generator, remove the failed `build`
directory first and configure again.

You can also build directly from a Visual Studio x64 Developer Command Prompt:

```bat
build_msvc.bat
```

### MinGW-w64 GCC (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/), open the **UCRT64** terminal, and
install the x64 GCC, CMake, and Ninja packages:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

In that terminal, change to this directory and confirm GCC targets x64 Windows:

```sh
g++ -dumpmachine
```

The output should be `x86_64-w64-mingw32`. Then configure and build with Ninja:

```sh
cmake -S . -B build/mingw -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++
cmake --build build/mingw
```

The MinGW build links its runtime statically, so the executable can be launched
outside the UCRT64 terminal. The OpenVR DLL remains beside the executable.

```sh
./build/mingw/vr-piston-animation.exe
```

## SteamVR startup

The installer registers the app with SteamVR and enables startup by default.
You can change this in SteamVR under **Settings > Startup / Shutdown > Choose
startup overlay apps**. Updates preserve your existing startup preference.

## Installer

Install NSIS 3 or later. After building, run `makensis` from this directory.
For the Visual Studio build:

```powershell
makensis installer\installer.nsi
```

For the MinGW build:

```powershell
makensis /DAPP_BUILD_DIR=build/mingw installer\installer.nsi
```

If you used `build_msvc.bat`, pass `build/msvc` as `APP_BUILD_DIR`. The
installer is written to `build\VR-Piston-Animation-Setup.exe`. It registers the
SteamVR manifest and the uninstaller removes that registration. The app appears
in SteamVR's startup overlay list; controller action bindings are not needed
for OSC control. Close the app before uninstalling it. If SteamVR is unavailable,
the installer cannot update its registration; the local files can still be
removed, but SteamVR may retain a stale app entry.

The required Windows OpenVR SDK files and the JSON header are included under
`third-party`. Each build method copies `openvr_api.dll` and the SteamVR manifest
beside the executable.

## Run

The app stays in the Windows notification
area. Right-click its tray icon to check whether animation is active, open the
log, or exit. Exiting restores the app's current offset and saves its parameter
values. Startup errors are shown in a dialog; detailed output is written to
`%LOCALAPPDATA%\VR Piston Animation\Logs\vr-piston-animation.log`.

```powershell
.\build\Release\vr-piston-animation.exe
```

If you built with `build_msvc.bat`:

```powershell
.\build\msvc\vr-piston-animation.exe
```

The app reads runtime config from `config.json` in the current working
directory. If it does not exist, the app creates one with defaults:

```json
{
  "amplitudeMeters": 0.3,
  "periodSeconds": 0.3,
  "fps": 120.0,
  "oscListenPort": 9001,
  "vrcOscSendPort": 9000,
  "oscEnabled": true,
  "updateSeated": true
}
```

Use a different config path with:

```powershell
.\build\Release\vr-piston-animation.exe --config my-config.json
```

- `amplitudeMeters`: maximum travel in meters before `VROffsetAmplitude` scaling.
- `periodSeconds`: seconds per full cycle when `VROffsetSpeed` is `1.0`.
- `fps`: update rate.
- `oscListenPort`: UDP port to listen for VRChat OSC.
- `vrcOscSendPort`: UDP port to send startup values to VRChat.
- `oscEnabled`: whether to listen for VRChat OSC.
- `updateSeated`: whether to update seated zero pose.

If your runtime cannot provide a seated zero pose, the app will print a warning
and continue in standing-only mode. Set `updateSeated` to `false` to force that
behavior.

## VRChat OSC

VRChat's default OSC ports are:

- External app to VRChat: UDP `9000`
- VRChat to external app: UDP `9001`

This app listens on UDP `9001` by default. In VRChat, enable OSC from the Action
Menu, then create or drive avatar parameters with these names:

| VRChat parameter | OSC path | Type | Range |
| --- | --- | --- | --- |
| `VROffsetEnabled` | `/avatar/parameters/VROffsetEnabled` | Bool | `false` / `true` |
| `VROffsetVertical` | `/avatar/parameters/VROffsetVertical` | Float | `-1` to `1` |
| `VROffsetHorizontal` | `/avatar/parameters/VROffsetHorizontal` | Float | `-1` to `1` |
| `VROffsetSpeed` | `/avatar/parameters/VROffsetSpeed` | Float | `0` to `1` |
| `VROffsetAmplitude` | `/avatar/parameters/VROffsetAmplitude` | Float | `0` to `1` |

Behavior:

- `VROffsetEnabled`: turns the animation on/off. The app starts disabled and
  will not animate until it receives `true`. While disabled, it does not write
  offset changes to OpenVR.
- `VROffsetVertical` and `VROffsetHorizontal`: form a 2D direction vector.
  Vertical maps to up/down, Horizontal maps to forward/back.
  If both values are `0`, the app keeps the previous non-zero direction.
- `VROffsetSpeed`: `0` stops phase movement, `1` uses the configured
  `periodSeconds`.
- `VROffsetAmplitude`: scales the maximum meter amplitude set by
  `amplitudeMeters`.

## Saved Parameters

On normal exit, the app saves these values to
`last-parameter-values.json` in the current working directory:

- `VROffsetVertical`
- `VROffsetHorizontal`
- `VROffsetSpeed`
- `VROffsetAmplitude`

`VROffsetEnabled` is not saved. On startup it is always treated as `false`.

On the next startup, the app loads the JSON file and sends the loaded values to
VRChat on UDP `9000`, along with `VROffsetEnabled = false`. This intentionally
overwrites avatar menu values changed while the app was not running, so the app
and VRChat start from the same values.

Example:

```json
{
  "amplitudeMeters": 0.2,
  "periodSeconds": 2.5,
  "fps": 120.0,
  "oscListenPort": 9001,
  "vrcOscSendPort": 9000,
  "oscEnabled": true,
  "updateSeated": false
}
```

Then send:

- `VROffsetEnabled = true`
- `VROffsetVertical = 1.0`
- `VROffsetHorizontal = 0.0`
- `VROffsetSpeed = 0.5`
- `VROffsetAmplitude = 0.75`

## License

This project is licensed under the [MIT License](LICENSE). The licenses for third-party dependencies are provided in `third-party`.

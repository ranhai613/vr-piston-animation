# VR Offset Animation

Tiny OpenVR test app that animates the chaperone working zero pose. It can run
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

The executable links against the OpenVR import library already present in this repository. At runtime, Windows also needs `openvr_api.dll` beside the executable or somewhere in `PATH`.

## Run

```powershell
.\build\Release\vr-offset-animation.exe
```

If you built with `build_msvc.bat`:

```powershell
.\build\msvc\vr-offset-animation.exe
```

Useful options:

```powershell
.\build\Release\vr-offset-animation.exe --amplitude 0.20 --period 4
```

OSC can choose the up/down plus forward/back direction:

```powershell
.\build\Release\vr-offset-animation.exe --amplitude 0.20 --period 4
```

- `--amplitude`: maximum travel in meters before `VROffsetAmplitude` scaling. Default: `0.3`
- `--period`: seconds per full cycle when `VROffsetSpeed` is `1.0`. Default: `0.3`
- `--fps`: update rate. Default: `120`
- `--osc-port`: UDP port to listen for VRChat OSC. Default: `9001`
- `--no-osc`: disable OSC listening
- `--no-seated`: only animate standing zero pose

If your runtime cannot provide a seated zero pose, the app will print a warning
and continue in standing-only mode. You can also force that behavior with
`--no-seated`.

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
  `--period`.
- `VROffsetAmplitude`: scales the maximum meter amplitude set by
  `--amplitude`.

## Saved Parameters

On normal exit, the app saves these values to
`vr-offset-animation-settings.json` in the current working directory:

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

```powershell
.\build\Release\vr-offset-animation.exe --amplitude 0.20 --period 2.5
```

Then send:

- `VROffsetEnabled = true`
- `VROffsetVertical = 1.0`
- `VROffsetHorizontal = 0.0`
- `VROffsetSpeed = 0.5`
- `VROffsetAmplitude = 0.75`

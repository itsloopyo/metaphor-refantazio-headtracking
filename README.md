# Metaphor: ReFantazio Head Tracking

![Metaphor: ReFantazio running with this mod](https://raw.githubusercontent.com/itsloopyo/metaphor-refantazio-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Metaphor: ReFantazio that moves the camera with your head while your mouse or controller keeps control of movement, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **6DOF positional tracking** - lean and peek by moving your head in space, not just rotating it.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Metaphor: ReFantazio on Steam](https://store.steampowered.com/app/2679460/Metaphor_ReFantazio/) (Windows x64).
- A head-tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or VR headset, or a phone app that sends OpenTrack UDP packets.
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Metaphor: ReFantazio**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/metaphor-refantazio-headtracking/releases) and extract it anywhere.
2. Double-click `install.cmd`. It installs the vendored Ultimate ASI Loader as `winmm.dll` and deploys `MetaphorHeadTracking.asi` next to `METAPHOR.exe`.
3. Configure OpenTrack (or your phone app) to send UDP output to `127.0.0.1:4242`.
4. Launch the game.

If the installer cannot find your game, point it at the install folder directly:

```powershell
# Positional argument:
install.cmd "D:\Games\Metaphor ReFantazio"

# Or set an environment variable before running:
set METAPHOR_PATH=D:\Games\Metaphor ReFantazio
```

### Manual Installation
To place the files by hand instead of running `install.cmd` (or when using the Nexus "extract to game folder" ZIP):

1. Copy the vendored Ultimate ASI Loader DLL into the game folder next to `METAPHOR.exe`, renamed to `winmm.dll`.
2. Copy `MetaphorHeadTracking.asi` into the same folder.
3. Launch the game once: the mod creates `CameraUnlock.ini` next to `METAPHOR.exe` (see Configuration).

The Nexus ZIP contains only the deploy-path files (no loader); you supply the ASI loader yourself.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Each action has a key and a Ctrl+Shift chord. Both are rebindable in `CameraUnlock.ini`
(`[Hotkeys]`); these are the defaults:

| Action              | Key         | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay (rotation + position)
2. Rotation only (positional tracking disabled)
3. Position only (rotational tracking disabled)
4. Back to normal

The tracking mode and the yaw mode you pick are saved to `CameraUnlock.ini` and come back at the
next start. `End` changes the current session only; `EnableOnStartup` decides whether tracking
is on when the game starts.

## Configuration

Apart from creating `CameraUnlock.ini` at startup when there is none, the mod writes to it only when a hotkey changes the tracking mode or the yaw mode. Edit it with the game closed.

The view moves five times as far as your head, so the position limits, which are in metres of head movement, allow five times that much camera travel.

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Metaphor: ReFantazio head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres of head movement, leaning left or right can move the view,
; which moves five times as far.
PositionLimitX=default
; How far, in metres of head movement, raising your head can move the view,
; which moves five times as far.
PositionLimitY=default
; How far, in metres of head movement, lowering your head can move the view,
; which moves five times as far.
PositionLimitYDown=default
; How far, in metres of head movement, leaning forward can move the view,
; which moves five times as far.
PositionLimitZ=default
; How far, in metres of head movement, leaning back can move the view,
; which moves five times as far.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default
; Diagnostics: in a diagnostic mode below, restarts the camera discovery or dumps the
; follow camera to the log. Does nothing in normal play.
DiagnosticKey=Insert, Ctrl+Shift+U

[Diagnostics]
; Diagnostics: true hooks the follow camera and dumps it on DiagnosticKey, with no head
; tracking. Leave off for play.
DumpFollowCam=false
; Diagnostics: true runs the camera discovery instead of head tracking. Leave off for play.
CameraDiscovery=false
; The game function the camera hook runs through. 0x0 = the one mapped for the running build.
; InjectHookRva=0x0
```
<!-- /cameraunlock:config -->

## Troubleshooting

The mod writes `MetaphorHeadTracking.log` next to `METAPHOR.exe` on every launch. Check it first to confirm the loader engaged and the UDP receiver is listening.

**Mod not loading**
- If `MetaphorHeadTracking.log` is missing, the loader did not load. Re-run `install.cmd` and confirm `winmm.dll` and `MetaphorHeadTracking.asi` are next to `METAPHOR.exe`.
- Make sure no other mod has already claimed `winmm.dll` in the game folder.

**No tracking response**
- Confirm OpenTrack (or your phone app) is sending UDP to `127.0.0.1:4242` and is actively tracking.
- If the tracker runs on a phone, use your PC's LAN IP as the destination and check Windows Firewall allows inbound UDP on port 4242.
- Press `End` (or `Ctrl+Shift+Y`) to confirm tracking is enabled. If the view sits off-centre, centre it in your tracker app (opentrack's Center bind, the CENTER button in Headcam).

**Jittery / unstable tracking**
- Raise the smoothing value your tracker uses toward 1.0 in `CameraUnlock.ini`: `[Smoothing] RemoteSmoothing` for a phone or other device on the network, `[Smoothing] LocalSmoothing` for a tracker running on this PC.
- Wireless and webcam trackers benefit most, which is why `RemoteSmoothing` starts at 0.15 while a local tracker gets none.

**Yaw feels wrong at extreme up/down angles**
- Toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. The installer ships no config, so `CameraUnlock.ini` keeps your settings.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's `.asi` plugin and leaves `CameraUnlock.ini` and `MetaphorHeadTracking.ini` in place. The Ultimate ASI Loader (`winmm.dll`) is only removed if this installer put it there. Run `uninstall.cmd /force` to remove it anyway.

## Building from Source

1. Initialize the `cameraunlock-core` and `third_party/minhook` git submodules.
2. Build with `pixi run build` (CMake plus the Visual Studio toolchain); output is `build/Release/MetaphorHeadTracking.asi`.
3. Package an installer ZIP with `pixi run package`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Atlus and SEGA for Metaphor: ReFantazio.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu.
- [OpenTrack](https://github.com/opentrack/opentrack) for head-tracking input.
- [Berzerker96](https://github.com/BerZerker96) for being such an enthusiastic head tracking fan, starting work on this mod, and buying me the game.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Atlus or SEGA. Use at your own risk.

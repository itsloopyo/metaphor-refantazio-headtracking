# Changelog

All notable changes to this project are documented in this file. The format is
based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [Unreleased]

### Added
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- One-shot `first tracker pose handed to the camera hook` line in the log,
  emitted after the pose is applied. Together with the receiver's own
  `First UDP packet received` line it separates packets never arriving from a
  pose that reached the camera hook without moving the view.

### Changed
- Settings move to `CameraUnlock.ini`, next to `METAPHOR.exe`. Earlier versions of the mod kept these settings in `MetaphorHeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `MetaphorHeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `MetaphorHeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `MetaphorHeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale or axis inversion you changed from its default. Set these in your tracker instead.
  - A hotkey set to Ctrl, Shift or Alt on its own. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord where it has one.
- `[Position] Limit` becomes the five position limits, `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` and `PositionLimitZBack`, in metres of head movement. The view moves five times as far as the head, so a `Limit` you set is written as `Limit` divided by 5 on all five and bounds the view as before. The default `Limit` of 1000 bounded nothing a tracker reaches; where you never set `Limit`, the five limits follow `Defaults.ini`, whose built-in values are 0.3, 0.2, 0.2, 0.4 and 0.1 metres of head movement. A `Limit` that `MetaphorHeadTracking.ini` held as `nan` or `inf` is imported as each limit's default. A `Limit` below 0 or above 50 has no place in `CameraUnlock.ini`: the mod then creates no `CameraUnlock.ini` and names the value in `MetaphorHeadTracking.log`. It runs that session on the settings it read from `MetaphorHeadTracking.ini` as this entry and the ones above describe them, so that `Limit` divided by 5 bounds all five limits and a sensitivity, scale or inversion you changed is not applied. It saves nothing, so the mode and yaw hotkeys change the current session only, and it reads `MetaphorHeadTracking.ini` again at the next start.
- `UdpPort` moves from `[General]` to `[Network]`. The diagnostic settings move to `[Diagnostics]`: `[Discovery] Enabled` is `CameraDiscovery` and `[Inject] HookRva` is `InjectHookRva`, beside `DumpFollowCam`. The diagnostic hotkey, `Insert` and `Ctrl+Shift+U` in either diagnostic mode, is `[Hotkeys] DiagnosticKey`.
- `[Position] Limit` now also bounds lowering your head. The dev build set every other bound from it and left the downward one at 0.20 of the offset after the gain of 5, which lowering your head by 4 cm reached (0cf8c60).
- An older version of the mod reads `MetaphorHeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `MetaphorHeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `MetaphorHeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The toggle and tracking mode hotkeys, fixed before, can be rebound.
- The tracking mode the mode hotkey picks and the yaw mode the yaw hotkey picks are saved to `CameraUnlock.ini` and come back at the next start. `End` still changes the current session only.
- The installer, the Nexus ZIP and Lopari ship no config file. The mod creates `CameraUnlock.ini` when it starts instead of writing `MetaphorHeadTracking.ini`, and `uninstall.cmd` leaves `CameraUnlock.ini` and `MetaphorHeadTracking.ini` in place.
- Every published ZIP, the Nexus one included, now carries `LICENSE` and a
  `THIRD-PARTY-NOTICES.md` that reproduces the full licence text of each
  bundled and statically linked component: Ultimate ASI Loader (MIT), MinHook
  (BSD-2-Clause, including the separate Hacker Disassembler Engine copyright)
  and cameraunlock-core (MIT). Naming a licence is not reproducing it, and both
  of those require the notice and disclaimer to travel with the binary.
- Removed recentring from the mod, including the `Home` / `Ctrl+Shift+T`
  hotkey. The tracker app owns the centre, so the mod keeping one of its own put
  a second centre in series with the tracker's and the two drifted apart. Centre
  in your tracker app instead (opentrack's Center bind, the CENTER button in
  Headcam).
- Replaced `[Smoothing] Factor` with `[Smoothing] LocalSmoothing` (default
  `0.0`) and `[Smoothing] RemoteSmoothing` (default `0.15`). The mod picks
  between them per connection from the packet's source address, and each covers
  rotation and position together.
- Removed the hidden 0.15 baseline smoothing floor. A tracker running on the
  same machine now gets zero-latency tracking by default instead of being
  silently smoothed against the user's setting.

### Removed
- The sensitivity, scale and axis inversion settings. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.

## [0.0.0] - 2026-06-26

### Added
- Initial scaffold release.
- Ultimate ASI Loader plugin entry point (MetaphorHeadTracking.asi).
- OpenTrack UDP receiver listening on port 4242.
- Hotkey handling (nav-cluster keys plus Ctrl+Shift chord alternatives).
- DXGI Present render hook.
- PE-fingerprint build profile registry for matching the running game build.
- Camera injection in progress (head movement does not yet move the view).

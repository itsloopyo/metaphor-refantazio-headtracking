# Changelog

All notable changes to this project are documented in this file. The format is
based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [Unreleased]

### Added
- One-shot `first tracker pose handed to the camera hook` line in the log,
  emitted after the pose is applied. Together with the receiver's own
  `First UDP packet received` line it separates packets never arriving from a
  pose that reached the camera hook without moving the view.

### Changed
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

## [0.0.0] - 2026-06-26

### Added
- Initial scaffold release.
- Ultimate ASI Loader plugin entry point (MetaphorHeadTracking.asi).
- OpenTrack UDP receiver listening on port 4242.
- Hotkey handling (nav-cluster keys plus Ctrl+Shift chord alternatives).
- DXGI Present render hook.
- PE-fingerprint build profile registry for matching the running game build.
- Camera injection in progress (head movement does not yet move the view).

# Third-Party Notices

MetaphorHeadTracking bundles or loads the following third-party components.

## Ultimate ASI Loader

- **Version:** v9.7.2
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Loads the head-tracking plugin (.asi) into the game process.
- **Bundled:** yes. Bundled in release ZIP as fallback; fetched latest within range at install time.

---

## MinHook

- **Version:** 1.3.3
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Installs the DXGI Present render hook.
- **Bundled:** yes. Statically linked into the plugin.

---

## OpenTrack

- **Version:** N/A (UDP protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** We implement its UDP pose protocol; no OpenTrack code is included.
- **Bundled:** no.

---

## cameraunlock-core

- **Version:** submodule (see .gitmodules)
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared head-tracking processing and math library.
- **Bundled:** yes. Statically linked into the plugin.

---

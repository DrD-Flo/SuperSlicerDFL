# SuperSlicerDFL vs. base SuperSlicer

SuperSlicerDFL ([DrD-Flo/SuperSlicerDFL](https://github.com/DrD-Flo/SuperSlicerDFL)) is a maintained
fork of [supermerill/SuperSlicer](https://github.com/supermerill/SuperSlicer) built for the DrD-Flo (DFL)
printer ecosystem (Voron and related machines). It tracks upstream SuperSlicer closely and regularly
merges upstream's `master_27` / `dev_27_6x` / `nightly_2.7.6x` branches, so the majority of the slicing
engine and GUI is unmodified upstream code. This document describes only the changes that are specific
to the DFL fork: distribution/branding, bundled printer profiles, build & CI infrastructure, and a
handful of engine/GUI features and bug fixes authored for this fork rather than pulled from upstream.

Baseline used for comparison: `upstream/master_27` (commit `1f3d287e9b`), the latest point at which
upstream's stable development line is a full ancestor of this fork's `master`. Commits authored by
`supermerill` (SuperSlicer's own maintainer) that reached DFL via early merges of `dev_27_62`,
`dev_27_63` and `nightly_2.7.63` before those branches landed in `master_27` are treated as upstream
work, not fork-specific changes, and are excluded below.

## 1. Branding, distribution & bundled profiles

- **Vendor profile bundle**: `resources/profiles` was converted into a git submodule pointing at
  [DrD-Flo/slic3r-profiles](https://github.com/DrD-Flo/slic3r-profiles) (`DFL-Printers.ini` /
  `DFL-Printers.idx`), replacing the upstream vendor bundle set. The submodule seed is bumped
  independently of the app version as the profile repo evolves (currently seed 2.5.1).
- **Auto-install & preselect on first run**: a fresh install now silently installs the DFL-Printers
  bundle and preselects the Voron 300 0.6mm printer, skipping the manual "Install x.y.z" bundle dialog
  and the first-run wizard's printer checkbox.
- **Vendor bundle sync tooling**: `sync-dfl-seed.sh` was added to pull and stamp the latest
  `DrD-Flo/slic3r-profiles` release into the submodule seed used by the auto-updater.
- **Auto-update robustness**: fixed crashes in the GitHub-hosted vendor config auto-update path
  (`download_vendor_bundles.py`, `Config/Snapshot.cpp`) and in configuration-snapshot handling when a
  vendor file referenced by a snapshot has since disappeared.
- **Splash screen**: replaced the stock SuperSlicer splash image with a DFL Benchy print photo
  (`resources/splashscreen/Benchy-Thumbnail-RAW-V2.jpg`), referenced via `colors.ini`'s
  `splash_screen_editor` key.
- **Default arrange spacing**: reduced the default object-arrange spacing from 6mm to 4mm
  (`ArrangeSettingsView.hpp`).
- **Third-party profile cleanup**: removed bundled third-party (non-DFL) printer profiles that shipped
  with upstream SuperSlicer, keeping the DFL vendor bundle as the only default profile set.

## 2. Slicing engine features

- **`filament_over_bridge_flow_ratio`**: new per-filament setting (percent, default 100) that
  multiplies the print profile's `over_bridge_flow_ratio`, mirroring the existing
  `filament_fill_top_flow_ratio` convention. Because over-bridge surfaces can't be distinguished from
  other infill by extrusion role at gcode-emit time, the modifier is applied to fill density during
  `group_fills`, and the surface-type substitution pass now also runs when only a filament (not the
  print profile) sets a non-default ratio. Exposed in Filament Settings > Filament > Flow, saved with
  filament presets, and stripped from Prusa-compatible config exports.
- **Concentric support pattern**: added `smpConcentric` as a new `support_material_pattern` option
  (alongside Rectilinear / Rectilinear grid / Honeycomb), generating support material as concentric
  contours instead of a linear/grid infill.

## 3. GUI fixes

- **macOS 14+ settings page bleed**: since macOS 14 Sonoma, `NSView.clipsToBounds` defaults to `NO` for
  apps built against the 14.0+ SDK, so rows of the settings page scrolled out of the
  `wxScrolledWindow` kept drawing over the preset combo, mode buttons and tab bar above it (the bundled
  Prusa fork of wxWidgets 3.2.0 predates upstream's Sonoma clipping fix). Added
  `mac_set_clips_to_bounds()`, applied per-view to the settings page container only, to avoid a global
  fix breaking `wxTextCtrl`/`wxSpinButton` rendering on macOS 26 Tahoe.
- **Blue Z-axis line artifact while panning**: `GLCanvas3D::on_render_timer` was forcing
  `m_show_z_axle = true` on every still-mouse-down pan tick, drawing a stray vertical blue line over
  the viewport; the assignment was dropped so panning no longer shows the Z axis indicator.
- **Update dialog button visibility**: fixed a button that could render hidden/inactive in the
  profile/app update dialog (`UpdateDialogs.cpp`, `PresetUpdater.cpp`).
- **`boost::too_many_args` crash on unsupported OpenGL**: the "unsupported OpenGL version" message
  passed 5 arguments through `format_wxstr` but the format string only referenced 4 distinct
  placeholders (`%2%` was reused for both the required and detected version), so `boost::format` threw
  and crashed the app before the warning could be shown. Renumbered placeholders `%1%`-`%5%` to match.

## 4. Build, packaging & CI

- **macOS DMG packaging**: replaced raw `hdiutil` calls with `create-dmg` (auto-installed via
  Homebrew), laying the `.app` next to an `/Applications` drop link so users drag-install instead of
  unzipping. Deduped the first-try/XProtect-retry `hdiutil` code paths, fixed a missing `exit` on
  retry-success, and removed dead debug code that could run as a stray shell command.
- **macOS "damaged" Gatekeeper error**: removed an empty `_CodeSignature` directory and added ad-hoc
  codesigning to the release packaging step, which was tripping Gatekeeper's "app is damaged" check on
  unsigned builds.
- **Toolchain compatibility patches**: patched the CGAL and libpng vendored dependencies to build under
  Xcode 26.4 (and earlier fixes for Xcode 16.4), and pinned the macOS SDK version used by
  `BuildMacOS.sh` to match.
- **Linux packaging fixes**: fixed `.deb` packaging, added a missing `hwproc` header include, and fixed
  system-`PNGConfig.cmake` shadowing the vendored static libpng on distros that ship a CMake package
  config for PNG (e.g. Debian 13 aarch64) by forcing Module-mode `find_package(PNG)` for that lookup.
- **Linux aarch64 support**: `BuildLinux.sh` now recognizes `aarch64`/`arm64` instead of exiting; AppImage
  naming (`build_appimage.sh.in`, `BuildLinuxImage.sh.in`) is derived from `uname -m` instead of being
  hardcoded to `x86_64`; added an `ubuntu-24.04-arm` CI workflow mirroring the existing GTK3 build.
  Also fixed the symlink upstream's Linux installer created for the gcodeviewer binary being broken.
- **Windows fixes**: fixed a permissions bug when writing a configuration snapshot on Windows
  (`AppConfig.cpp`) and corrected the Windows build version stamping.
- **Windows/macOS CI overhaul**: consolidated and rewrote the GitHub Actions workflows for Windows,
  macOS (Intel and Apple Silicon) and Linux release-candidate builds under DFL branding/repo naming,
  removed the redundant `*_debug` workflow variants inherited from upstream, and switched Linux release
  artifact paths to match the naming `BuildLinux.sh` actually produces (uploads were previously
  silently skipping all Linux artifacts because the workflow looked for
  `SuperSlicerDFL-linux-x64-GTK3.tgz` while the build produced `SuperSlicer-linux-x64-GTK3.tgz`).
- **`create_release.py`**: fixed the release script reusing an existing output directory across runs.
- **In-app application updates**: wired the built-in app updater (inherited from PrusaSlicer via
  upstream) to the DFL fork so users no longer download installers manually. `SLIC3R_GITHUB` /
  `SLIC3R_DOWNLOAD` in `version.inc` now point at `DrD-Flo/SuperSlicerDFL`, so the startup check and
  the "Check for Application Updates" menu query this fork's GitHub releases. Release-tag parsing
  (`AppUpdater.cpp`) tolerates prefixed tags (e.g. `summer-2.7.63.0`), asset matching is
  case-insensitive (matches `SuperSlicer-windows.msi` / `SuperSlicer-macOS-*.dmg`), Apple Silicon
  detection uses `__aarch64__` (the old `__arm__` check made arm Macs download the intel dmg), and the
  post-download Finder open uses the actual dmg volume name instead of a hardcoded
  `/Volumes/PrusaSlicer`.

  Release requirements for the updater to work:
  - **Versioning scheme (upstream parity)**: the first three components track the upstream SuperSlicer
    base (`2.7.63`); the fourth component is the DFL release counter. Bump only the fourth component
    (`2.7.63.1`, `2.7.63.2`, ...) in `SLIC3R_RC_VERSION` / `SLIC3R_RC_VERSION_DOTS` for each DFL
    release, and reset it to `.0` when merging a newer upstream base. This keeps the fork's version
    from ever passing upstream's, while still giving the updater a strictly increasing version.
    Both comparisons involved handle this: the app's semver compare treats `2.7.63 < 2.7.63.1 <
    2.7.64.0`, and the MSI upgrades across fourth-component-only bumps because Windows Installer
    ignores the fourth field and `winInstaller.wxs.in` sets `AllowSameVersionUpgrades="yes"`.
  - Never re-release the same version under a `-2` tag suffix: the parser reads `-N` as a semver
    prerelease, which sorts below the plain version, so users would never be offered it. Bump the
    fourth component instead.
  - Prefer plain numeric tags (e.g. `2.7.63.1`); prefixed tags still parse but keep the version part
    canonical.
  - Keep the release asset naming: `*windows*.msi`, `*macOS-arm*.dmg`, `*macOS-intel*.dmg` (matched
    case-insensitively on `win`/`msi`, `macos`/`arm`/`dmg`).

## 5. Upstream hardening carried ahead of a tagged release

The fork also carries a batch of upstream robustness fixes (division-by-zero, buffer-overflow, null-
pointer and logic-precedence fixes across `libslic3r` geometry/tree-support code, 3MF/AMF import, and
EXIF parsing, contributed upstream by Evgeny Zislis and others) that were merged into DFL's history
ahead of their inclusion in a tagged `supermerill/SuperSlicer` release. These are upstream fixes, not
DFL-authored changes, but are called out here because they aren't yet present in the last official
SuperSlicer release tag.

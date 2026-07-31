# QXlsx source integration

- Canonical library name: QXlsx
- Upstream version: 1.5.1.1
- Upstream tag: `v1.5.1.1`
- Upstream commit: `8a13e1c86e5d4fb5e3b2fb09c7b632514f1d54ca`
- Upstream repository: <https://github.com/QtExcel/QXlsx>
- Source archive: <https://github.com/QtExcel/QXlsx/archive/refs/tags/v1.5.1.1.zip>
- Archive SHA-256: `5358EC4605E0084AB99E79C993E83364C94CF42CCE079DB579AC29A6E5E07D5F`
- License: MIT; see `LICENSE`

The upstream `QXlsx/` library directory, repository-level `README.md`, and
license are vendored without source changes. The README is required by QXlsx's
upstream CPack configuration. Examples, CI configuration, and standalone
application projects from the source archive are intentionally omitted because
they are not required by this repository's consumer target.

The repository consumes the upstream static target `QXlsx::QXlsx` through
`add_subdirectory`. QXlsx uses Qt Core and Qt Gui/GuiPrivate and introduces no
additional non-Qt runtime dependency. Its build products stay in the selected
CMake build directory; no prebuilt library or runtime DLL is committed.

Validated project toolchain:

- Visual Studio 2022 / MSVC 19.40.33811
- x64
- Qt 6.5.3 `msvc2019_64`
- Debug and Release are separate CMake configurations

Validation completed with both a minimal `QXlsx::QXlsx` consumer and the full
`EddyCurrentBench` target in Debug and Release configurations. Validation was
compile/link only; no executable or hardware-control path was run.

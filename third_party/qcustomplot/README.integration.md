# QCustomPlot source integration

- Canonical library name: QCustomPlot
- Upstream version: 2.1.1
- Upstream tag: `v2.1.1`
- Upstream commit: `ae32723839f903a52fee57274f8f1715245e1a9c`
- Upstream repository: <https://gitlab.com/ecme2/QCustomPlot>
- Source archive: <https://gitlab.com/ecme2/QCustomPlot/-/archive/v2.1.1/QCustomPlot-v2.1.1.tar.gz>
- Archive SHA-256: `509024A79607FA00E80EDC8F6579649E2D0F30BC37FF67D45C09D4755D8B7A0B`
- License: GNU GPL version 3; see `GPL.txt`

The complete upstream `src/` tree is vendored without source changes. This is the
structured, non-amalgamated source layout. The upstream `src/qcp-staticlib.pro`
file is retained as provenance, while this repository builds the same source set
through CMake.

The repository exposes the static target `QCustomPlot::QCustomPlot`, backed by the
real target `qcustomplot`. It depends on `Qt6::Widgets` and `Qt6::PrintSupport`.
The library target is declared with `EXCLUDE_FROM_ALL`, so QCustomPlot is not
built, linked, installed, or deployed as part of the application unless a
consumer explicitly opts in or the `qcustomplot` target is built directly.

Validated project toolchain target:

- Visual Studio 2022 / MSVC 17.10
- x64
- Qt 6.5.3 `msvc2019_64`
- Debug and Release are separate CMake configurations

The upstream 2.1.1 release documents compatibility through Qt 6.4. Compatibility
with this project's Qt 6.5.3 is established only by the repository's explicit
Debug and Release target builds.

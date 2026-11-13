# bearpaw-scan

[![CI](https://github.com/byhat/bearpaw-scan/actions/workflows/build.yml/badge.svg)](https://github.com/byhat/bearpaw-scan/actions/workflows/build.yml)
[![License: GPL-2.0+](https://img.shields.io/badge/license-GPL--2.0%2B-blue.svg)](LICENSE)

**Qt6/QML scanner application for the Mustek BearPaw 2448 CU Pro** (`USB 055f:0408`).
Standalone GUI: find the device, scan, watch the log, get a PNG. No vendor
software and no SANE installation required — the engine (the
[libbearpawcore](https://github.com/byhat/bearpaw-sane) core, vendored here)
talks to the scanner directly over libusb using a fully reverse-engineered
protocol.


| | |
|---|---|
| **Device** | Mustek BearPaw 2448 CU Pro (`055f:0408`) |
| **Mode** | Color, 8 bit, 300 dpi, full A4 |
| **Toolkit** | Qt 6 / QML, no other runtime dependencies |
| **Binaries** | Single self-contained executable (the recorded session is embedded) |
| **License** | GPL-2.0-or-later |

## Install

### Arch Linux

Grab the `bearpaw-scan-<version>-x86_64` binary from the
[Releases](https://github.com/byhat/bearpaw-scan/releases) page and run it:

```bash
chmod +x bearpaw-scan-*-x86_64
./bearpaw-scan-*-x86_64          # add sudo if no udev rule is installed
```

Dependencies: `qt6-base`, `qt6-declarative`, `libusb`.

### From source (any distro)

```bash
cmake -S . -B build && cmake --build build -j
./build/bearpaw-scan          # add sudo if no udev rule is installed
```

Requirements: cmake, a C++17 compiler, Qt6 (Base + Declarative), libusb.
Debian/Ubuntu: `apt install cmake g++ qt6-base-dev qt6-declarative-dev libusb-1.0-0-dev`.

## Usage

1. Connect the scanner (and detach it from any VM if it was passed through).
2. Press **Find device**.
3. Press **Scan** — the carriage runs a full A4 pass (~50 s), progress and
   the full log are shown live and written to `logs/bearpawscan_YYYYMMDD.log`.
4. The result PNG appears in the window and is saved to `scans/`.

Running under `sudo` is handled gracefully (Wayland → XWayland fallback,
DISPLAY/XAUTHORITY detection). For passwordless use install the udev rule
from the [bearpaw-sane](https://github.com/byhat/bearpaw-sane) driver —
then `sudo` is not needed.

## How it works

```
src/core/    vendored libbearpawcore (libusb transport, recorded
             session replay with absolute pacing, GBR line decoder)
src/         Qt6 GUI: ScanWorker (engine wrapper), LogModel, QML UI
data/        session.ops — the recorded vendor command sequence,
             embedded into the binary at link time
```

The protocol was reverse-engineered from a live USB capture of the
original Windows software; see the companion projects
[bearpaw-sane](https://github.com/byhat/bearpaw-sane) (SANE driver) for
details and credits.

## Roadmap

- [ ] grayscale mode toggle in the UI
- [ ] resolution / area selection (75/150/300 dpi)
- [ ] AppImage / Flatpak builds in CI

## License

GPL-2.0-or-later. The protocol was reverse-engineered from traffic
captured from the user's own device.

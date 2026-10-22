# Optional BLE BMS radio firmware

The BLE BMS extension adds one BMS central connection alongside the existing
VESC Tool peripheral connection. It exchanges bounded, versioned `BB` messages
with the matching controller extension over the existing 115200-baud VESC UART.
The BMS decoders, saved device settings and VESC Tool QML page run in the
controller repository; this directory contains the radio transport and debug
support.

## Build isolation and supported chips

`BLE_BMS` defaults to `0`. Ordinary nRF52832 and nRF52840 builds retain the
upstream link counts, logging setting, UART forwarding, ESB scheduling, linker
layout and board selection. They do not compile or initialize the BLE BMS
module or its retained fault handler. `storage.h` declares the existing
`storage.c` configuration object with `extern`, allowing modern GCC builds
without relying on common symbols.

`BLE_BMS=1` currently supports **nRF52840 only**, using nRF5 SDK
15.3.0_59ac345, SoftDevice S140 6.1.1 and the tested GNU Arm Embedded
10.3.1 toolchain. It selects the feature linker script
`ble_bms/ld_sd_52840_bms.ld`, one central link and one peripheral link.
The feature reserves the first 64 KiB of RAM for the SoftDevice and retains
fault information at `0x2003ff00`; the application stack ends immediately below
that retained area.

An nRF52832 feature request fails at makefile evaluation. A future S132 port
needs its own measured SoftDevice RAM reservation, linker layout and retained
fault address; the current nRF52840 addresses are unsuitable for its 64 KiB of
RAM. nRF51 uses a separate firmware/SDK architecture and has no implementation
here. A board containing an nRF chip is therefore not sufficient to establish
compatibility with this image.

BLE BMS builds omit the legacy ESB remote timeslot scheduler because it caused
an S140 assertion when scanning began. This policy applies to every feature
board profile. Stock builds retain the upstream ESB scheduler. Simultaneous
ESB remote operation and BLE BMS operation are not supported by this extension.

## Build commands

Run these commands from the radio repository. Supply the SDK and compiler paths
through `SDK_ROOT`, `GNU_INSTALL_ROOT` and `GNU_VERSION` on each invocation, or
configure those paths as described in the upstream README. For example, add
`SDK_ROOT=/opt/nRF5_SDK_15.3.0_59ac345`
`GNU_INSTALL_ROOT=/opt/gcc-arm-none-eabi-10.3-2021.10/bin/`
`GNU_VERSION=10.3.1` to a command. Make and the SDK expect paths without spaces.
The sibling controller's `documentation/ble_bms/build_firmware.ps1` creates
temporary drive aliases for Windows paths and runs the full build matrix.

```sh
# Stock upstream behavior; distinct directories make audit results explicit.
make BLE_BMS=0 IS_52832=0 OUTPUT_DIRECTORY=_build_stock52840
make BLE_BMS=0 IS_52832=1 OUTPUT_DIRECTORY=_build_stock52832

# BLE BMS with the upstream nRF52840 built-in board's pins and device name.
make BLE_BMS=1 IS_52832=0 OUTPUT_DIRECTORY=_build_bms

# Tronic 250R: verified radio RX=P0.06, TX=P0.05; testing name "250R beta".
make BLE_BMS=1 IS_52832=0 OUTPUT_DIRECTORY=_build_tronic250r \
  'build_args=-DBLE_BMS_TRONIC_250R=1'

# Spare nRF52840 DK: RX=P0.08, TX=P0.06, LED=P0.13.
make BLE_BMS=1 IS_52832=0 OUTPUT_DIRECTORY=_build_devkit \
  'build_args=-DBLE_BMS_DEVKIT=1'

# Example using an existing upstream board selection; keep its name and pins.
make BLE_BMS=1 IS_52832=0 OUTPUT_DIRECTORY=_build_bms_stormcore \
  'build_args=-DMODULE_BUILTIN=0 -DMODULE_STORMCORE=1'

# Synthetic JBD peripheral for a separate DK; 2=ANT and 3=Daly.
# This is not a VESC radio image.
make BLE_BMS=1 IS_52832=0 OUTPUT_DIRECTORY=_build_sim_1 BMS_SIM_PROTOCOL=1
```

Without an explicit output directory, stock builds use `_build` and feature
builds use `_build_bms`. Give each board profile and simulator a separate
directory. Feature builds reject an explicit path to the stock `_build`
directory, including `./_build`. Other explicit `OUTPUT_DIRECTORY` values are
the caller's responsibility: make does not track changes to `build_args` as
source dependencies, so reusing one directory for different flags requires
cleaning it first. The feature fault object also depends on the feature linker
script, ensuring that a layout-only change causes the image to relink. The upstream
`merge_hex` command also uses the shared `hex/merged.*` output, so copy any
merged artifact to its own identified destination before building another
variant. The documented commands build only; they do not program hardware.

## Board profiles and porting

Only the Tronic profile changes the default advertised name to `250R beta`.
Saved upstream radio name settings can override that default. Other profiles
retain their upstream name and pin selection.

The Tronic defaults live in `tronic_pins.h` and can be overridden without
editing `main.c`, for example by adding `-DBLE_BMS_TRONIC_RX=6` and
`-DBLE_BMS_TRONIC_TX=5` to that profile's `build_args`. Its unverified LED and
alternate TX pins remain disconnected. The DK profile is a test fixture and
does not identify another controller board's wiring.

For another nRF52840 module, select or add an explicit board profile, verify the
UART route and electrical levels, confirm the SoftDevice/flash layout and
recovery path, and test a complete firmware-version request over BLE. Merely
advertising or reading UART selection registers does not prove both UART
directions. The matching controller target must also enable and configure the
controller-side extension; flashing this radio alone does not add the scan UI
or BMS integration to an unrelated controller firmware.

## Debug support

Feature builds enable RTT logging and retain the last fault across a software
reset in a separate 256-byte RAM region. `ble_bms_fault.c` contains the fault
record; `debug/gpio_uart_probe.c` is a temporary RAM-only pin probe, excluded
from production images. USB diagnostics and host test/programming tools live
under the matching controller's `documentation/ble_bms/` directory. Follow its
backup and recovery procedure before programming an installed radio; an
upstream mass erase command is not a substitute for that procedure.

The Tronic UART mapping and BMS/Tool concurrent connection have bench evidence.
Compilation of another board profile is not physical validation of its wiring,
recovery, phone behavior, BMS protocols or ESB coexistence.

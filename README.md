# nrf52_vesc

## Tronic 250R BLE BMS radio — experimental prerelease

**[Download compiled radio images](https://github.com/Amprides/nrf52_vesc/releases/tag/tronic250r-ble-bms-v0.1.0-beta.1)**
· **[Windows installer and controller firmware](https://github.com/Amprides/bldc/releases/tag/tronic250r-ble-bms-v0.1.0-beta.1)**
· **[USB installation / BMS setup](https://github.com/Amprides/bldc/blob/tronic250r-ble-bms-v0.1.0-beta.1/documentation/ble_bms/INSTALL.md)**

This fork adds an opt-in BLE BMS central connection alongside VESC Tool BLE
peripheral/UART forwarding. The matching controller firmware decodes JBD, ANT
and Daly telemetry, stores the battery selection, and embeds the **BMS Scan**
QML page. See [the radio extension guide](ble_bms/README.md) for transport,
debugging and build details.

The compiled profile is for the Tronic 250R's nRF52840, with **RX P0.06, TX
P0.05, 115200 baud**, nRF5 SDK 15.3.0 and S140 6.1.1. The combined radio BIN
contains the MBR/SoftDevice/application and starts at flash offset 0; the
application starts at `0x26000`. It is not a controller BIN and must not be
uploaded through VESC Tool's controller Custom File upload. The controller BIN
does not update this radio automatically: use the matched Windows USB installer.

Back up the original radio/settings before installing. The installer preserves
FDS/UICR, verifies readback and can restore each unit's original radio code from
its own backup. **Do not run the generic mass-erase/upload instructions below
on the onboard Tronic module.** They are retained as upstream development-board
documentation and do not describe this board's USB recovery procedure.

Default builds keep `BLE_BMS=0` and retain upstream behavior. BLE BMS builds
currently require nRF52840/S140 and omit the legacy ESB remote scheduler.
nRF52832/nRF51, other controller pin profiles and ESB coexistence are not
qualified by this prerelease. A short JBD bench test exists on earlier firmware;
the new installer restoration cycle, one-hour soak and physical ANT/Daly tests
remain pending. Published downloads are experimental prerelease assets.

Clone this repository as `nrf52-vesc` beside the controller checkout named
`upstream-bldc` when using the controller's build helpers. Exact source revisions
and SHA-256 hashes are supplied in the release manifest. Building is optional;
compiled BIN/HEX files are already provided in the release.

## Upstream documentation

This is code for the NRF52832 and the NRF52840 for communicating between the VESC and VESC Tool (linux and mobile) over BLE. After uploading the firmware, the NRF can be connected to the VESC using the RX and TX pins chosen in main.c, and the BLE scanner in VESC Tool should be able to find it and connect. Note that the UART port on the VESC must be enabled with a baud rate of 115200 for this to work. The NRF can also communicate with the VESC Remote at the same time as it runs BLE.  

The code can be build with the NRF52 SDK by changing the path in Makefile. There is a variable in the makefile called IS_52832. If it is set to 1, the code is built for the NRF52832, otherwise it is built for the NRF52840. The difference between the two is which files are included from the NRF SDK and whether USB-CDC support is used in main.c. For now USB-CDC is only useful for printing debug information, but later it might be possible to connect VESC Tool over it.

This is how to connect an STLINK V2 to the NRF52 for uploading the code:

| NRF52         | STLINK V2     |
| ------------- |---------------|
| GND           | GND           |
| VDD           | 3.3V          |
| SDO           | SWDIO         |
| SCL           | SWCLK         |

The first time the code is uploaded mass_erase must be run, and the softdevice must be uploaded. This can be done with the following make rules:

```bash
make mass_erase
```

```bash
make upload_sd
```

after that and after future changes only the code itself needs to be uploaded:

```bash
make upload
```

After the code is uploaded, the NRF52 can be connected to the VESC in the following way:

| NRF52         | VESC          |
| ------------- |---------------|
| GND           | GND           |
| VDD           | VCC (3.3V)    |
| Px.y (TX)     | RX            |
| Px2.y2 (RX)   | TX            |

Note that a 10 uF ceremaic capacitor between VCC and GND close to the NRF52 module might be needed if the cables are long. Otherwise the connection can become slow and unstable.


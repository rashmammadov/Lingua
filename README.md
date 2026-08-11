# Lingua

[![Build](https://github.com/rashmammadov/Lingua/actions/workflows/build.yml/badge.svg)](https://github.com/rashmammadov/Lingua/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Lingua is a physical macOS keyboard-layout indicator built with an ESP32-C3,
three OLED displays, and Bluetooth Low Energy. It keeps the active language in
the center and the neighboring layouts on the side displays, then animates the
transition when you switch input sources.

The current implementation recognizes English (`EN`), Russian (`RU`), and
Azerbaijani (`AZ`) layouts.

![Lingua animating between macOS keyboard layouts](docs/lingua-demo.gif)

## How it works

```text
macOS input source
        │
        ▼
Swift bridge ── Bluetooth LE ──► ESP32-C3 ──► 3 × SSD1306 OLED
```

The macOS bridge watches for keyboard-layout changes and sends a compact state
message over BLE. The firmware renders all three framebuffers and updates the
displays in parallel so that transitions stay synchronized.

## Hardware

- AirM2M Core ESP32-C3 development board
- Three SSD1306 128×64 I²C OLED displays
- Jumper wires and a suitable USB cable/power source
- A Mac with Bluetooth Low Energy

All displays normally use the same I²C address, so Lingua gives each one its own
software-I²C data and clock pair:

| Display | SDA | SCL | Default role |
| --- | ---: | ---: | --- |
| OLED 1 | GPIO 8 | GPIO 5 | Next layout |
| OLED 2 | GPIO 9 | GPIO 6 | Current layout |
| OLED 3 | GPIO 4 | GPIO 7 | Previous layout |

Connect every display's `VCC` and `GND` according to its module specification.
The pin mapping can be changed near the top of [`src/main.cpp`](src/main.cpp).

## Install the firmware

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the
   [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode),
   or install PlatformIO Core.
2. Clone this repository:

   ```sh
   git clone https://github.com/rashmammadov/Lingua.git
   cd Lingua
   ```

3. Connect the ESP32-C3 and build/upload the firmware:

   ```sh
   pio run
   pio run --target upload
   ```

   If `pio` is not on your `PATH`, run the same tasks from the PlatformIO panel
   in VS Code.

The board advertises itself as `Language Display` after startup. Set
`DEVICE_UPSIDE_DOWN` to `0` in [`src/main.cpp`](src/main.cpp) if your assembled
device is not mounted in the orientation used by the original build.

## Build and run the macOS bridge

Install the Xcode Command Line Tools if needed:

```sh
xcode-select --install
```

Then compile and run the bridge from the repository root:

```sh
mkdir -p .build
swiftc tools/input_language_bridge.swift \
  -framework Cocoa -framework Carbon \
  -framework CoreBluetooth \
  -o .build/input-language-bridge
.build/input-language-bridge
```

On first launch, macOS may ask for Bluetooth access. Also allow the terminal or
launcher under **System Settings → Privacy & Security → Accessibility** and
**Input Monitoring**. These permissions let the bridge distinguish the F15/F16
input-switching shortcuts and choose the matching animation.

Keep the bridge running while using Lingua. It automatically discovers the
display, reconnects after interruptions, and resends the latest state.

## Customization

- Add or change language mappings in `shortLanguage(for:)` inside
  [`tools/input_language_bridge.swift`](tools/input_language_bridge.swift).
- Change the BLE device name or UUIDs in both the Swift bridge and firmware;
  the values must match.
- Adjust display pins, rotation, fonts, and animation timing in
  [`src/main.cpp`](src/main.cpp).

## BLE message format

The writable BLE characteristic accepts UTF-8 messages in this form:

```text
PREVIOUS|CURRENT|NEXT|DIRECTION|MODE|TOGGLE
```

For example: `RU|EN|AZ|LEFT|CAROUSEL|--`. The firmware also accepts these
messages over the USB serial port at 115200 baud, which is useful for testing.

The BLE link is intentionally unauthenticated because Lingua is designed as a
nearby personal desktop accessory; do not use this protocol for sensitive data.

## Development

```sh
pio run
swiftc tools/input_language_bridge.swift \
  -framework Cocoa -framework Carbon \
  -framework CoreBluetooth \
  -o .build/input-language-bridge
```

GitHub Actions runs both build checks for every push and pull request. See
[`CONTRIBUTING.md`](CONTRIBUTING.md) before proposing a change.

## License

Lingua is available under the [MIT License](LICENSE).

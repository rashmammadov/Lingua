# macOS input-language bridge

The bridge watches the active macOS keyboard layout and sends the current,
previous, and next language codes to the Lingua display over Bluetooth LE.

## Requirements

- macOS
- Xcode Command Line Tools (`xcode-select --install`)
- Accessibility and Input Monitoring permission for the terminal or app that
  runs the bridge (needed to detect the F15/F16 switching direction)

## Build and run

From the project root:

```sh
mkdir -p .build
swiftc tools/input_language_bridge.swift \
  -framework Cocoa -framework Carbon \
  -framework CoreBluetooth \
  -o .build/input-language-bridge
.build/input-language-bridge
```

The bridge automatically finds the BLE device named `Language Display`,
reconnects if needed, and sends `EN`, `RU`, or `AZ` whenever the input source
changes. Keep it running while the ESP32 is powered.

To support another layout, extend `shortLanguage(for:)` in
`input_language_bridge.swift`.

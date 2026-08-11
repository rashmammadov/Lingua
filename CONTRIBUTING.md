# Contributing to Lingua

Thanks for helping improve Lingua. Bug reports, documentation fixes, support
for additional keyboard layouts, and hardware adaptations are welcome.

## Before opening a pull request

1. Create a focused branch from `main`.
2. Keep hardware-specific constants easy to find and document any wiring
   changes in the README.
3. Build the firmware with `pio run`.
4. On macOS, compile the bridge:

   ```sh
   mkdir -p .build
   swiftc tools/input_language_bridge.swift \
     -framework Cocoa -framework Carbon \
     -framework CoreBluetooth \
     -o .build/input-language-bridge
   ```

5. Explain what changed and how you tested it in the pull request.

Please do not commit PlatformIO output, local editor configuration, or compiled
bridge binaries.

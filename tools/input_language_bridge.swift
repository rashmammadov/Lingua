import Cocoa
import Carbon
import CoreBluetooth

let notificationCenter = DistributedNotificationCenter.default()
let serviceUUID = CBUUID(string: "7B7F0001-6C2B-4A73-9B1F-0F5A0B7E1000")
let languageUUID = CBUUID(string: "7B7F0002-6C2B-4A73-9B1F-0F5A0B7E1000")

func shortLanguage(for source: TISInputSource) -> String {
    guard let properties = TISGetInputSourceProperty(source, kTISPropertyInputSourceID) else { return "--" }
    let id = Unmanaged<CFString>.fromOpaque(properties).takeUnretainedValue() as String
    if id.contains("Russian") || id.contains("Cyrillic") {
        return "RU"
    }
    if id.contains("Azeri") || id.contains("Azerbaijani") {
        return "AZ"
    }
    if id.contains("US") || id.contains("ABC") || id.contains("British") {
        return "EN"
    }
    return id
}

func inputLanguages() -> [String] {
    guard let unmanaged = TISCreateInputSourceList(nil, false) else { return [] }
    let list = unmanaged.takeRetainedValue() as NSArray
    var result: [String] = []
    for case let source as TISInputSource in list {
        let language = shortLanguage(for: source)
        if ["EN", "RU", "AZ"].contains(language) && !result.contains(language) {
            result.append(language)
        }
    }
    return result
}

func languageTriplet() -> String {
    let languages = inputLanguages()
    guard let current = TISCopyCurrentKeyboardInputSource()?.takeRetainedValue(),
          let currentID = TISGetInputSourceProperty(current, kTISPropertyInputSourceID) else { return "--|--|--" }
    let id = Unmanaged<CFString>.fromOpaque(currentID).takeUnretainedValue() as String
    let currentLanguage = shortLanguage(for: current)
    guard !languages.isEmpty else { return "\(currentLanguage)|\(currentLanguage)|\(currentLanguage)" }
    let index = languages.firstIndex(of: currentLanguage) ?? 0
    let previous = languages[(index + languages.count - 1) % languages.count]
    let next = languages[(index + 1) % languages.count]
    _ = id
    return "\(previous)|\(currentLanguage)|\(next)"
}

final class BLEBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?
    var characteristic: CBCharacteristic?
    var pendingValue: String?
    var latestValue: String?
    var writeInProgress = false

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func send(_ value: String) {
        latestValue = value
        pendingValue = value
        sendLatestIfPossible()
    }

    func sendLatestIfPossible() {
        guard !writeInProgress,
              let value = pendingValue,
              let peripheral,
              let characteristic else { return }
        pendingValue = nil
        writeInProgress = true
        peripheral.writeValue(Data(value.utf8), for: characteristic, type: .withResponse)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            let connected = central.retrieveConnectedPeripherals(withServices: [serviceUUID])
            if let device = connected.first {
                self.peripheral = device
                device.delegate = self
                central.connect(device)
            } else {
                central.scanForPeripherals(withServices: [serviceUUID])
            }
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                       advertisementData: [String : Any], rssi RSSI: NSNumber) {
        self.peripheral = peripheral
        central.stopScan()
        peripheral.delegate = self
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("BLE connected: \(peripheral.name ?? "Language Display")")
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        self.peripheral = nil
        self.characteristic = nil
        self.writeInProgress = false
        central.scanForPeripherals(withServices: [serviceUUID])
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) {
            peripheral.discoverCharacteristics([languageUUID], for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        characteristic = service.characteristics?.first(where: { $0.uuid == languageUUID })
        // The ESP32 may have restarted and cleared the OLEDs, so resend the
        // latest state after every new BLE connection.
        if pendingValue == nil {
            pendingValue = latestValue
        }
        sendLatestIfPossible()
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        writeInProgress = false
        sendLatestIfPossible()
    }
}

let bridge = BLEBridge()
var lastValue = ""
var lastCurrentLanguage: String?
var pendingInputValue: String?
var updateWorkItem: DispatchWorkItem?
var shortcutDirection: String?
var shortcutDisplayMode: String?
var shortcutTimestamp = Date.distantPast

func rememberShortcutDirection(keyCode: Int) {
    switch keyCode {
    case kVK_F15:
        // macOS: Select the previous input source.
        shortcutDirection = "RIGHT"
        shortcutDisplayMode = "TOGGLE"
        shortcutTimestamp = Date()
        print("Shortcut: PREVIOUS → RIGHT")
    case kVK_F16:
        // macOS: Select next source in Input menu.
        shortcutDirection = "LEFT"
        shortcutDisplayMode = "CAROUSEL"
        shortcutTimestamp = Date()
        print("Shortcut: NEXT → LEFT")
    default:
        break
    }
}

func keyboardEventCallback(
    proxy: CGEventTapProxy,
    type: CGEventType,
    event: CGEvent,
    userInfo: UnsafeMutableRawPointer?
) -> Unmanaged<CGEvent>? {
    if type == .keyDown {
        let keyCode = Int(event.getIntegerValueField(.keyboardEventKeycode))
        DispatchQueue.main.async {
            rememberShortcutDirection(keyCode: keyCode)
        }
    }
    return Unmanaged.passUnretained(event)
}

func installKeyboardEventTap() -> CFMachPort? {
    let mask = CGEventMask(1 << CGEventType.keyDown.rawValue)
    guard let tap = CGEvent.tapCreate(
        tap: .cgSessionEventTap,
        place: .headInsertEventTap,
        options: .listenOnly,
        eventsOfInterest: mask,
        callback: keyboardEventCallback,
        userInfo: nil
    ) else {
        fputs("Cannot monitor F15/F16. Enable Accessibility and Input Monitoring for Terminal.\n", stderr)
        return nil
    }

    let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
    CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
    CGEvent.tapEnable(tap: tap, enable: true)
    return tap
}

func sendIfChanged() {
    let triplet = languageTriplet()
    if triplet != lastValue {
        let parts = triplet.split(separator: "|").map(String.init)
        let current = parts.count == 3 ? parts[1] : "--"
        let languages = inputLanguages()
        var direction = "NONE"
        var displayMode = "CAROUSEL"
        var toggleLanguage = "--"

        if let requested = shortcutDirection,
           Date().timeIntervalSince(shortcutTimestamp) < 1.0 {
            direction = requested
            displayMode = shortcutDisplayMode ?? "CAROUSEL"
            if displayMode == "TOGGLE" {
                // F15 switches to the last-used source. The source that was
                // current immediately before the switch is therefore its pair.
                toggleLanguage = lastCurrentLanguage ?? parts[0]
            }
            shortcutDirection = nil
            shortcutDisplayMode = nil
        } else if let old = lastCurrentLanguage,
           let oldIndex = languages.firstIndex(of: old),
           let newIndex = languages.firstIndex(of: current),
           languages.count > 1 {
            if newIndex == (oldIndex + 1) % languages.count {
                direction = "LEFT"
            } else if newIndex == (oldIndex + languages.count - 1) % languages.count {
                direction = "RIGHT"
            }
        }

        lastCurrentLanguage = current
        lastValue = triplet
        let value = "\(triplet)|\(direction)|\(displayMode)|\(toggleLanguage)"
        pendingInputValue = value
        updateWorkItem?.cancel()
        let work = DispatchWorkItem {
            if let pendingInputValue {
                bridge.send(pendingInputValue)
                print("Input languages: \(pendingInputValue)")
            }
        }
        updateWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.08, execute: work)
    }
}

sendIfChanged()
let keyboardEventTap = installKeyboardEventTap()

notificationCenter.addObserver(
    forName: Notification.Name("AppleSelectedInputSourcesChangedNotification"),
    object: nil,
    queue: .main
) { _ in
    sendIfChanged()
}

RunLoop.main.run()

import Foundation
import Combine
import IOKit
import IOKit.hid

/// One raw HID input report from the mouse (report ID byte included at [0]).
struct RawReport: Identifiable {
    let id = UUID()
    let time = Date()
    let reportID: UInt8
    let bytes: [UInt8]
    var hex: String { bytes.map { String(format: "%02x", $0) }.joined(separator: " ") }
    var kind: String {
        switch reportID {
        case 0x03: return "mouse"
        case 0x01: return "keyboard"
        case 0x02: return "consumer"
        case 0x27: return "vendor FF07"
        case 0x2A, 0x2C, 0x2D: return "vendor FF0B"
        default: return String(format: "report 0x%02x", reportID)
        }
    }
}

/// One entry of the device's HID Simple Haptic Controller waveform table.
struct Waveform: Identifiable, Hashable {
    let ordinal: Int        // what you put in the Manual Trigger field
    let usage: UInt16       // HID haptics usage (0x1003 = Click …)
    let durationMs: Int
    var id: Int { ordinal }
    var name: String { Waveform.names[usage] ?? String(format: "Waveform 0x%04X", usage) }
    var symbol: String { Waveform.symbols[usage] ?? "waveform" }

    static let names: [UInt16: String] = [
        0x1001: "None", 0x1002: "Stop", 0x1003: "Click", 0x1004: "Buzz", 0x1005: "Rumble",
        0x1006: "Press", 0x1007: "Release", 0x1008: "Hover", 0x1009: "Success", 0x100A: "Error",
        0x100B: "Ink", 0x1012: "Collide", 0x1013: "Align", 0x1014: "Step", 0x1015: "Grow",
    ]
    static let symbols: [UInt16: String] = [
        0x1003: "cursorarrow.click.2", 0x1008: "hand.point.up.left", 0x1009: "checkmark.circle",
        0x100A: "xmark.octagon", 0x1012: "arrow.right.to.line", 0x1013: "align.horizontal.center",
        0x1014: "arrow.up.and.down", 0x1015: "arrow.up.left.and.arrow.down.right",
    ]
    /// Table read from a Surface Mouse 2nd Edition (fw 5.29) on 2026-10-16.
    /// Used until the connected device answers with its own.
    static let measured: [Waveform] = [
        Waveform(ordinal: 3,  usage: 0x1008, durationMs: 9),
        Waveform(ordinal: 4,  usage: 0x1012, durationMs: 26),
        Waveform(ordinal: 5,  usage: 0x1013, durationMs: 9),
        Waveform(ordinal: 6,  usage: 0x1014, durationMs: 11),
        Waveform(ordinal: 7,  usage: 0x1015, durationMs: 200),
        Waveform(ordinal: 8,  usage: 0x1009, durationMs: 70),
        Waveform(ordinal: 9,  usage: 0x100A, durationMs: 147),
        Waveform(ordinal: 10, usage: 0x1003, durationMs: 11),
    ]
    static func ordinal(for usage: UInt16, in list: [Waveform]) -> Int? { list.first { $0.usage == usage }?.ordinal }
}

/// The Surface Mouse 2nd Edition over IOKit HID: enumerates, parses its input
/// reports, reads the haptic table, and fires haptic triggers. Everything is
/// public IOKit; no driver, no private framework.
final class MouseDevice: ObservableObject {
    static let vendorID  = 0x045E
    static let productID = 0x0880
    static let hapticOutputReport: UInt8 = 0x43   // [ordinal, intensity 0…4]
    static let hapticTableReport:  UInt8 = 0x42   // duration list + waveform list

    @Published private(set) var connected = false
    @Published private(set) var buttons: UInt8 = 0            // bit0 L, bit1 R, bit2 wheel, bit3/4 extra
    @Published private(set) var wheelTotal = 0
    @Published private(set) var lastMotion = CGSize.zero
    @Published private(set) var waveforms: [Waveform] = Waveform.measured
    @Published private(set) var tableFromDevice = false
    @Published private(set) var battery: Int?
    @Published private(set) var firmware = "—"
    @Published private(set) var serial = "—"
    @Published private(set) var transport = "—"
    @Published private(set) var log: [RawReport] = []
    @Published private(set) var reportsSeen = 0
    @Published private(set) var lastError = ""
    /// Set when IOHIDDeviceOpen was refused (Input Monitoring not granted).
    @Published private(set) var openRefused = false

    var onReport: ((RawReport) -> Void)?
    var onButtons: ((UInt8, UInt8) -> Void)?   // (old, new)
    var onWheel: ((Int) -> Void)?
    var onConnect: (() -> Void)?

    private let manager: IOHIDManager
    private var device: IOHIDDevice?
    private let inputBuffer = UnsafeMutablePointer<UInt8>.allocate(capacity: 1024)
    private let io = DispatchQueue(label: "com.alcybr.thrum.hid", qos: .userInteractive)
    private var batteryTimer: Timer?
    private var retryTimer: Timer?
    private var motionReset: DispatchWorkItem?
    private var loggedAbsent = false

    init() {
        manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
        let match: [String: Any] = [kIOHIDVendorIDKey: Self.vendorID, kIOHIDProductIDKey: Self.productID]
        IOHIDManagerSetDeviceMatching(manager, match as CFDictionary)
        let ctx = Unmanaged.passUnretained(self).toOpaque()
        IOHIDManagerRegisterDeviceMatchingCallback(manager, { ctx, _, _, dev in
            guard let ctx else { return }
            Unmanaged<MouseDevice>.fromOpaque(ctx).takeUnretainedValue().attach(dev)
        }, ctx)
        IOHIDManagerRegisterDeviceRemovalCallback(manager, { ctx, _, _, dev in
            guard let ctx else { return }
            Unmanaged<MouseDevice>.fromOpaque(ctx).takeUnretainedValue().detach(dev)
        }, ctx)
        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
        // The manager is only used to enumerate; each device is opened directly so
        // a refused open (Input Monitoring) is visible and retryable.
        retryTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in self?.enumerate() }
        batteryTimer = Timer.scheduledTimer(withTimeInterval: 900, repeats: true) { [weak self] _ in self?.refreshBattery() }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in self?.enumerate() }
    }

    /// Attach any matching device the manager already knows about.
    func enumerate() {
        guard device == nil else { return }
        guard let set = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>, !set.isEmpty else {
            if !loggedAbsent { loggedAbsent = true; DebugLog.log("no Surface Mouse in IOHIDManager enumeration") }
            return
        }
        for dev in set { attach(dev); if device != nil { break } }
    }

    private func attach(_ dev: IOHIDDevice) {
        guard device == nil else { return }
        let r = IOHIDDeviceOpen(dev, IOOptionBits(kIOHIDOptionsTypeNone))
        guard r == kIOReturnSuccess else {
            lastError = String(format: "IOHIDDeviceOpen failed 0x%08x", r)
            let refused = (r == kIOReturnNotPermitted)
            if !openRefused || !refused {
                DebugLog.log(String(format: "Surface Mouse found but IOHIDDeviceOpen failed 0x%08x (Input Monitoring?)", r))
            }
            openRefused = refused
            return
        }
        openRefused = false
        device = dev
        let ctx = Unmanaged.passUnretained(self).toOpaque()
        IOHIDDeviceRegisterInputReportCallback(dev, inputBuffer, 1024, { ctx, _, _, _, reportID, report, len in
            guard let ctx else { return }
            let me = Unmanaged<MouseDevice>.fromOpaque(ctx).takeUnretainedValue()
            me.handleInput(reportID: UInt8(truncatingIfNeeded: reportID),
                           bytes: Array(UnsafeBufferPointer(start: report, count: len)))
        }, ctx)
        IOHIDDeviceScheduleWithRunLoop(dev, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)

        if let v = IOHIDDeviceGetProperty(dev, kIOHIDVersionNumberKey as CFString) as? Int {
            firmware = String(format: "%x.%02x", v >> 8, v & 0xff)     // bcdDevice 0x0529 → 5.29
        }
        serial = IOHIDDeviceGetProperty(dev, kIOHIDSerialNumberKey as CFString) as? String ?? "—"
        transport = IOHIDDeviceGetProperty(dev, kIOHIDTransportKey as CFString) as? String ?? "—"
        connected = true
        lastError = ""
        DebugLog.log("Surface Mouse attached (fw \(firmware), \(transport))")
        readHapticTable()
        refreshBattery()
        onConnect?()
    }

    /// When the mouse last went away (Bluetooth LE sleeps after a few idle minutes).
    private(set) var lastDetach: Date?

    private func detach(_ dev: IOHIDDevice) {
        guard let cur = device, cur == dev else { return }
        lastDetach = Date()
        DebugLog.log("Surface Mouse detached")
        IOHIDDeviceUnscheduleFromRunLoop(cur, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
        IOHIDDeviceClose(cur, IOOptionBits(kIOHIDOptionsTypeNone))
        device = nil
        connected = false
        buttons = 0
        battery = nil
    }

    // MARK: Input

    private func handleInput(reportID: UInt8, bytes: [UInt8]) {
        reportsSeen += 1
        let rep = RawReport(reportID: reportID, bytes: bytes)
        if reportID == 0x03, bytes.count >= 6 {
            let b = bytes[1] & 0x1f
            let dx = Int(Int16(bitPattern: UInt16(bytes[2]) | (UInt16(bytes[3]) << 8)))
            let dy = Int(Int16(bitPattern: UInt16(bytes[4]) | (UInt16(bytes[5]) << 8)))
            let wheel = bytes.count >= 8 ? Int(Int16(bitPattern: UInt16(bytes[6]) | (UInt16(bytes[7]) << 8))) : 0
            if b != buttons {
                let old = buttons
                buttons = b
                onButtons?(old, b)
                append(rep)
            }
            if dx != 0 || dy != 0 {
                lastMotion = CGSize(width: dx, height: dy)
                motionReset?.cancel()
                let w = DispatchWorkItem { [weak self] in self?.lastMotion = .zero }
                motionReset = w
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: w)
            }
            if wheel != 0 { wheelTotal += wheel; onWheel?(wheel) }
        } else {
            append(rep)
        }
        onReport?(rep)
    }

    private func append(_ r: RawReport) {
        log.append(r)
        if log.count > 150 { log.removeFirst(log.count - 150) }
        // Also on disk (reports.log next to settings): the mouse's own button,
        // key and vendor reports only, never pointer motion.
        ReportLog.write("\(r.kind) \(r.hex)")
    }

    func clearLog() { log.removeAll() }

    // MARK: Feature / output reports

    /// Synchronous GET_REPORT(feature). Call off the main thread.
    func readFeature(_ id: UInt8) -> [UInt8]? {
        guard let dev = device else { return nil }
        var buf = [UInt8](repeating: 0, count: 512)
        buf[0] = id
        var len: CFIndex = 512
        let r = buf.withUnsafeMutableBufferPointer {
            IOHIDDeviceGetReport(dev, kIOHIDReportTypeFeature, CFIndex(id), $0.baseAddress!, &len)
        }
        guard r == kIOReturnSuccess else { return nil }
        return Array(buf[0..<len])
    }

    func readFeatureAsync(_ id: UInt8, _ done: @escaping ([UInt8]?) -> Void) {
        io.async { [weak self] in
            let r = self?.readFeature(id)
            DispatchQueue.main.async { done(r) }
        }
    }

    /// Feature 0x42: 50 durations (u16 ms) then 50 waveform usages (u16).
    func readHapticTable() {
        io.async { [weak self] in
            guard let self, let raw = self.readFeature(Self.hapticTableReport), raw.count >= 201 else { return }
            var list: [Waveform] = []
            for i in 0..<50 {
                let dur = Int(raw[1 + 2 * i]) | (Int(raw[2 + 2 * i]) << 8)
                let usage = UInt16(raw[101 + 2 * i]) | (UInt16(raw[102 + 2 * i]) << 8)
                if usage >= 0x1003 && usage <= 0x2FFF {
                    list.append(Waveform(ordinal: i + 3, usage: usage, durationMs: dur))
                }
            }
            DispatchQueue.main.async {
                if !list.isEmpty { self.waveforms = list; self.tableFromDevice = true }
            }
        }
    }

    /// Output 0x43: Manual Trigger = waveform ordinal, Intensity 0…4.
    func trigger(ordinal: Int, intensity: Int) {
        guard let dev = device else { return }
        io.async { [weak self] in
            var r: [UInt8] = [Self.hapticOutputReport, UInt8(clamping: ordinal), UInt8(clamping: max(0, min(4, intensity)))]
            let res = r.withUnsafeMutableBufferPointer {
                IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, CFIndex(Self.hapticOutputReport), $0.baseAddress!, 3)
            }
            if res != kIOReturnSuccess {
                DispatchQueue.main.async { self?.lastError = String(format: "haptic SetReport failed 0x%08x", res) }
            }
        }
    }

    // MARK: Battery (Bluetooth LE battery service, via system_profiler; slow, so rare + background)

    func refreshBattery() {
        guard connected else { return }
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/usr/sbin/system_profiler")
            p.arguments = ["SPBluetoothDataType", "-json"]
            let pipe = Pipe(); p.standardOutput = pipe; p.standardError = FileHandle.nullDevice
            guard (try? p.run()) != nil else { return }
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            p.waitUntilExit()
            guard let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let arr = root["SPBluetoothDataType"] as? [[String: Any]] else { return }
            var pct: Int?
            for ctl in arr {
                for (_, v) in ctl {
                    guard let devs = v as? [[String: Any]] else { continue }
                    for entry in devs {
                        for (name, props) in entry {
                            guard name.localizedCaseInsensitiveContains("Surface Mouse"),
                                  let d = props as? [String: Any] else { continue }
                            if let s = d["device_batteryLevelMain"] as? String ?? d["device_batteryLevel"] as? String,
                               let n = Int(s.replacingOccurrences(of: "%", with: "")) { pct = n }
                        }
                    }
                }
            }
            DispatchQueue.main.async { if let pct { self?.battery = pct } }
        }
    }
}


/// Raw non-motion reports (buttons, keyboard chords, vendor channels) for the
/// Diagnostics pane; size-capped like the debug log.
enum ReportLog {
    static let url = Settings.url.deletingLastPathComponent().appendingPathComponent("reports.log")
    private static let q = DispatchQueue(label: "com.alcybr.thrum.reports")
    static func write(_ s: String) {
        FileLog.append("\(ISO8601DateFormatter().string(from: Date())) \(s)\n", to: url, on: q)
    }
}

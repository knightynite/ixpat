import Foundation

/// Per-device pointer and scroll acceleration for this mouse only, through
/// Apple's own `hidutil` (the public SDK has no header for the HID event-system
/// client). Values are 16.16 fixed point: System Settings' 0…3 tracking scale
/// times 65536; -65536 turns acceleration off.
enum PointerSettings {
    static let matching = "{\"VendorID\":\(MouseDevice.vendorID),\"ProductID\":\(MouseDevice.productID)}"

    static func apply(speed: Double, noAcceleration: Bool, scrollSpeed: Double) {
        let accel = noAcceleration ? -65536 : Int((speed * 65536).rounded())
        let scroll = Int((scrollSpeed * 65536).rounded())
        DispatchQueue.global(qos: .utility).async {
            _ = run(["property", "--matching", matching, "--set", "{\"HIDMouseAcceleration\":\(accel)}"])
            _ = run(["property", "--matching", matching, "--set", "{\"HIDScrollAcceleration\":\(scroll)}"])
        }
    }

    /// Put the mouse back on the system-wide values.
    static func restoreSystem() {
        let scaling = (UserDefaults.standard.persistentDomain(forName: UserDefaults.globalDomain)?["com.apple.mouse.scaling"] as? Double) ?? 1.0
        let wheel = (UserDefaults.standard.persistentDomain(forName: UserDefaults.globalDomain)?["com.apple.scrollwheel.scaling"] as? Double) ?? 0.3125
        apply(speed: scaling, noAcceleration: scaling < 0, scrollSpeed: wheel)
    }

    @discardableResult
    private static func run(_ args: [String]) -> String {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/hidutil")
        p.arguments = args
        let pipe = Pipe(); p.standardOutput = pipe; p.standardError = FileHandle.nullDevice
        guard (try? p.run()) != nil else { return "" }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        return String(decoding: data, as: UTF8.self)
    }
}

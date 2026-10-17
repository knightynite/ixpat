import AppKit
import CoreGraphics
import IOKit

/// Synthesises a trackpad Force Click for the mouse: press and hold the left button
/// without moving, and the app under the pointer gets the same pressure events a
/// Force Touch trackpad sends, so Look Up, link previews and Quick Look open.
///
/// Measured on macOS 27 (AppKit disassembly + probes, 2026-10-17):
///  • AppKit builds an NSEvent of type `pressure` (34) from a CG gesture event (type 29)
///    whose field 110 is 32 (kIOHIDEventTypeForce); stage is field 143, pressure field 148,
///    behaviour field 144 (5 = deep click), phase field 132 in CG numbering (1 began,
///    2 changed, 4 ended). Posting type 34 directly is nulled by the window server.
///  • NSImmediateActionGestureRecognizer only starts if the *mouse-down* reports
///    `associatedEventsMask` with the pressure bit, which AppKit derives from the event's
///    device ID (private field 87): it must be the registry ID of an `AppleMultitouchDevice`
///    that supports force. Stamping the real mouse-down with the trackpad's ID is enough.
///  • The user preference `com.apple.trackpad.forceClick` (global domain) must be 1 or
///    AppKit ignores the gesture; apps pick a change up live through the distributed
///    notification `com.apple.ForceClickBehaviorDidChangeNotification`.
enum ForceClick {
    static let prefKey = "com.apple.trackpad.forceClick"
    static let prefNotification = "com.apple.ForceClickBehaviorDidChangeNotification"

    // Private CGEvent fields (see above).
    static let fieldDeviceID = CGEventField(rawValue: 87)!
    static let fieldHIDType = CGEventField(rawValue: 110)!
    static let fieldPhase = CGEventField(rawValue: 132)!
    static let fieldStage = CGEventField(rawValue: 143)!
    static let fieldBehavior = CGEventField(rawValue: 144)!
    static let fieldPressure = CGEventField(rawValue: 148)!

    /// Registry IDs of the multitouch devices AppKit will accept as Force Touch sources.
    /// Main thread; cheap enough to call on every settings push.
    static func touchDeviceIDs() -> [Int64] {
        var it: io_iterator_t = 0
        guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleMultitouchDevice"), &it) == KERN_SUCCESS else { return [] }
        defer { IOObjectRelease(it) }
        var ids: [Int64] = []
        while case let s = IOIteratorNext(it), s != 0 {
            var id: UInt64 = 0
            if IORegistryEntryGetRegistryEntryID(s, &id) == KERN_SUCCESS, id != 0 { ids.append(Int64(bitPattern: id)) }
            IOObjectRelease(s)
        }
        return ids
    }

    /// Whether the system-wide "Force Click and haptic feedback" preference is on.
    static var systemEnabled: Bool {
        (CFPreferencesCopyAppValue(prefKey as CFString, kCFPreferencesAnyApplication) as? NSNumber)?.boolValue ?? false
    }

    /// Turn the system preference on (what the Trackpad settings toggle writes) and tell running apps.
    static func enableSystemPreference() {
        CFPreferencesSetAppValue(prefKey as CFString, NSNumber(value: 1), kCFPreferencesAnyApplication)
        CFPreferencesAppSynchronize(kCFPreferencesAnyApplication)
        DistributedNotificationCenter.default().postNotificationName(Notification.Name(prefNotification), object: nil,
                                                                     userInfo: nil, deliverImmediately: true)
    }

    /// One pressure event. `phase` uses CG numbering: 1 began, 2 changed, 4 ended.
    static func pressureEvent(source: CGEventSource?, at p: CGPoint, stage: Int64, pressure: Double, phase: Int64, device: Int64) -> CGEvent? {
        guard let ev = CGEvent(source: source) else { return nil }
        ev.type = CGEventType(rawValue: 29)!
        ev.location = p
        ev.timestamp = CGEventTimestamp(mach_absolute_time())
        ev.setIntegerValueField(fieldHIDType, value: 32)
        ev.setIntegerValueField(fieldStage, value: stage)
        ev.setDoubleValueField(fieldPressure, value: pressure)
        ev.setIntegerValueField(fieldBehavior, value: 5)
        ev.setIntegerValueField(fieldPhase, value: phase)
        if device != 0 { ev.setIntegerValueField(fieldDeviceID, value: device) }
        return ev
    }
}

/// Tap-thread snapshot of the force-click settings.
struct ForceConfig {
    var enabled: Bool
    var delay: Double            // seconds the button must stay still before the press "goes deep"
    var deviceID: Int64          // multitouch device ID to stamp on the mouse-down (0 = none available)
    var touchDevices: Set<Int64> // devices that already force-click on their own: leave their events alone
    static let off = ForceConfig(enabled: false, delay: 0.35, deviceID: 0, touchDevices: [])
}

import AppKit
import Combine
import QuartzCore

/// Recognises the action button from the mouse's raw reports.
///
/// Measured 2026-10-16 (fw 5.29): the firmware decides the gesture itself and
/// sends a different Windows chord for each, as keyboard report 0x01:
///   press        → modifiers 0x0A (Win+Shift) + key 0x16 (S)     = Snipping Tool
///   double press → modifiers 0x08 (Win) + a synthesised LEFT CLICK = Click to Do
///   hold         → modifiers 0x0A (Win+Shift) + key 0x72 (F23)   = the Copilot key, held until release
/// So there is no timing to do here: each chord maps straight to a gesture. The
/// synthesised click and the chords are hidden from apps by the event tap.
final class ActionButtonController: ObservableObject {
    @Published private(set) var pressed = false
    @Published private(set) var lastGesture = "—"

    var onGesture: ((ButtonGesture) -> Void)?
    /// The button was released after a hold gesture fired (Action Ring commit).
    var onHoldEnd: (() -> Void)?
    /// Called at every press; return true to consume the press (e.g. the ring
    /// is open in toggle mode and this press commits it).
    var pressIntercept: (() -> Bool)?
    /// Whether the chords are hidden from apps at all; pushed from main.
    var swallowEnabled: Bool { get { _swallow.value } set { _swallow.value = newValue } }
    private let _swallow = Locked(true)

    private enum Chord { case snip, win, copilot }
    /// Written on main (device reports), read on the tap thread (shouldSwallow).
    private struct State {
        var chord: Chord?
        var clickSeen = false
        var deviceKeyUntil: CFTimeInterval = 0      // matching key events are swallowed until then
        var swallowClicksUntil: CFTimeInterval = 0  // the synthesised click is swallowed until then
    }
    private let state = Locked(State())

    // MARK: Device side (main thread)

    func handle(_ r: RawReport) {
        let b = r.bytes
        switch r.reportID {
        case 0x01 where b.count >= 3:
            let mods = b[1]
            let keys = Set(b[2...].filter { $0 != 0 })
            if mods != 0 || !keys.isEmpty { state.mutate { $0.deviceKeyUntil = CACurrentMediaTime() + 0.4 } }
            if mods & 0x22 != 0, keys.contains(0x16) { chordDown(.snip) }
            else if mods & 0x22 != 0, keys.contains(0x72) { chordDown(.copilot) }
            else if mods & 0x88 != 0, keys.isEmpty { chordDown(.win) }
            else if mods == 0, keys.isEmpty { chordUp() }
        case 0x03 where b.count >= 2:
            // The "double press" chord is Win + a left click the firmware fakes.
            let s = state.value
            if s.chord == .win, b[1] & 0x01 != 0, !s.clickSeen {
                state.mutate { $0.clickSeen = true; $0.swallowClicksUntil = CACurrentMediaTime() + 0.5 }
                fire(.doublePress)
            }
        default: break
        }
    }

    private func chordDown(_ c: Chord) {
        guard state.value.chord != c else { return }
        state.mutate { $0.chord = c; $0.clickSeen = false }
        pressed = true
        if let pressIntercept, pressIntercept() {
            // Consumed (ring commit). The Win chord's fake click still follows and must stay hidden.
            state.mutate { $0.chord = nil; if c == .win { $0.swallowClicksUntil = CACurrentMediaTime() + 0.5 } }
            pressed = false
            return
        }
        switch c {
        case .snip:    fire(.press)
        case .copilot: fire(.hold)        // the firmware already waited for the hold
        case .win:     break              // the fake click that follows confirms the double press
        }
    }

    private func chordUp() {
        let s = state.value
        guard let c = s.chord else { return }
        state.mutate { $0.chord = nil; $0.clickSeen = false }
        pressed = false
        switch c {
        case .copilot: onHoldEnd?()
        case .win:     if !s.clickSeen { fire(.doublePress) }   // click never came: still Click to Do
        case .snip:    break
        }
    }

    private func fire(_ g: ButtonGesture) {
        lastGesture = g.title
        DebugLog.log("action button gesture: \(g.rawValue)")
        onGesture?(g)
    }

    // MARK: Event-tap side (tap thread): hide the firmware's chords from apps

    func shouldSwallow(_ event: CGEvent, type: CGEventType) -> Bool {
        guard swallowEnabled else { return false }
        let s = state.value
        let now = CACurrentMediaTime()
        let chordActive = now < s.deviceKeyUntil || s.chord != nil
        switch type {
        case .keyDown, .keyUp:
            // The chords arrive as ⌘⇧S and ⌘⇧F23: swallow ⌘-key events only while the mouse is sending one.
            return chordActive && event.flags.contains(.maskCommand)
        case .flagsChanged:
            // Only the chord's own modifier transitions (⌘ / ⇧), not the user's keyboard.
            guard chordActive else { return false }
            let k = event.getIntegerValueField(.keyboardEventKeycode)
            return k == 54 || k == 55 || k == 56 || k == 60
        case .leftMouseDown:
            // The release is paired with the swallowed press by the event monitor.
            return s.chord == .win || now < s.swallowClicksUntil
        default:
            return false
        }
    }
}

enum ActionRunner {
    static func run(_ a: ButtonAction) {
        switch a {
        case .none: break
        case .actionRing: break   // handled by the app delegate (needs the ring items)
        case .gestures: break     // handled by the app delegate (needs the hold state)
        case .volumeUp:       mediaKey(0)
        case .volumeDown:     mediaKey(1)
        case .mute:           mediaKey(7)
        case .nextTrack:      mediaKey(17)
        case .previousTrack:  mediaKey(18)
        case .brightnessUp:   mediaKey(2)
        case .brightnessDown: mediaKey(3)
        case .spaceLeft:      press(123, [.maskControl])
        case .spaceRight:     press(124, [.maskControl])
        case .missionControl:
            NSWorkspace.shared.openApplication(at: URL(fileURLWithPath: "/System/Applications/Mission Control.app"),
                                               configuration: NSWorkspace.OpenConfiguration(), completionHandler: nil)
        case .spotlight:   press(49, [.maskCommand])
        case .screenshot:  press(23, [.maskCommand, .maskShift])
        case .lockScreen:  press(12, [.maskCommand, .maskControl])
        case .showDesktop: press(103, [])
        case .playPause:   mediaKey(16)
        case .launchApp(let path):
            guard !path.isEmpty else { return }
            NSWorkspace.shared.openApplication(at: URL(fileURLWithPath: path),
                                               configuration: NSWorkspace.OpenConfiguration(), completionHandler: nil)
        case .shortcut(let key, let flags, _):
            guard key >= 0 else { return }
            press(CGKeyCode(key), CGEventFlags(rawValue: flags))
        case .shell(let cmd):
            guard !cmd.isEmpty else { return }
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/bin/zsh")
            p.arguments = ["-lc", cmd]
            try? p.run()
        }
    }

    static func press(_ key: CGKeyCode, _ flags: CGEventFlags) {
        let src = CGEventSource(stateID: .hidSystemState)
        guard let d = CGEvent(keyboardEventSource: src, virtualKey: key, keyDown: true),
              let u = CGEvent(keyboardEventSource: src, virtualKey: key, keyDown: false) else { return }
        d.flags = flags; u.flags = flags
        d.post(tap: .cghidEventTap)
        u.post(tap: .cghidEventTap)
    }

    static func mediaKey(_ key: Int32) {
        func ev(_ down: Bool) -> NSEvent? {
            NSEvent.otherEvent(with: .systemDefined, location: .zero,
                               modifierFlags: NSEvent.ModifierFlags(rawValue: down ? 0xa00 : 0xb00),
                               timestamp: 0, windowNumber: 0, context: nil, subtype: 8,
                               data1: Int((key << 16) | ((down ? 0xa : 0xb) << 8)), data2: -1)
        }
        ev(true)?.cgEvent?.post(tap: .cghidEventTap)
        ev(false)?.cgEvent?.post(tap: .cghidEventTap)
    }

    static func shortcutLabel(keyCode: Int, flags: CGEventFlags) -> String {
        var s = ""
        if flags.contains(.maskControl) { s += "⌃" }
        if flags.contains(.maskAlternate) { s += "⌥" }
        if flags.contains(.maskShift) { s += "⇧" }
        if flags.contains(.maskCommand) { s += "⌘" }
        return s + keyName(keyCode)
    }

    static func keyName(_ code: Int) -> String {
        let names: [Int: String] = [
            0: "A", 1: "S", 2: "D", 3: "F", 4: "H", 5: "G", 6: "Z", 7: "X", 8: "C", 9: "V", 11: "B", 12: "Q", 13: "W",
            14: "E", 15: "R", 16: "Y", 17: "T", 18: "1", 19: "2", 20: "3", 21: "4", 22: "6", 23: "5", 25: "9", 26: "7",
            28: "8", 29: "0", 31: "O", 32: "U", 34: "I", 35: "P", 37: "L", 38: "J", 40: "K", 45: "N", 46: "M",
            36: "↩", 48: "⇥", 49: "Space", 51: "⌫", 53: "⎋", 96: "F5", 97: "F6", 98: "F7", 99: "F3", 100: "F8",
            101: "F9", 103: "F11", 109: "F10", 111: "F12", 118: "F4", 120: "F2", 122: "F1", 105: "F13",
            123: "←", 124: "→", 125: "↓", 126: "↑",
        ]
        return names[code] ?? "key \(code)"
    }
}

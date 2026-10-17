import Foundation
import Combine
import SwiftUI

/// Things that can trigger a haptic pulse.
enum HapticEvent: String, CaseIterable, Codable, Identifiable {
    case leftDown, leftUp, rightDown, rightUp, middleDown
    case scrollStep, screenEdge, dragEnd, windowSnap, appSwitch, actionButton
    case menuHighlight, textSelect, textLine, ringDetent, ringOpen, ringCommit, freeSpinOn, freeSpinOff
    case leftHold, rightHold, freeSpinTexture, gestureStep, gestureLimit, gestureSwipe

    var id: String { rawValue }
    var title: String {
        switch self {
        case .leftDown:     return "Left click"
        case .leftUp:       return "Left release"
        case .rightDown:    return "Right click"
        case .rightUp:      return "Right release"
        case .middleDown:   return "Wheel click"
        case .scrollStep:   return "Scroll notch"
        case .screenEdge:   return "Pointer hits a screen edge"
        case .dragEnd:      return "Drag & drop lands"
        case .windowSnap:   return "Window tiles"
        case .appSwitch:    return "App switches"
        case .actionButton: return "Action button"
        case .menuHighlight: return "Menu item highlights"
        case .textSelect:   return "Text selection, per word"
        case .textLine:     return "Text selection crosses a line"
        case .ringDetent:   return "Action Ring detent"
        case .ringOpen:     return "Action Ring opens"
        case .ringCommit:   return "Action Ring runs an action"
        case .freeSpinOn:   return "Wheel enters free-spin"
        case .freeSpinOff:  return "Wheel re-engages the ratchet"
        case .leftHold:     return "Left button held"
        case .rightHold:    return "Right button held"
        case .freeSpinTexture: return "Free-spin texture"
        case .gestureStep:  return "Gesture step (volume, brightness)"
        case .gestureLimit: return "Gesture hits a limit"
        case .gestureSwipe: return "Gesture swipe (track, desktop)"
        }
    }
    var detail: String {
        switch self {
        case .leftDown, .rightDown: return "From the mouse's own report. The firmware may already click on its own."
        case .leftUp, .rightUp:     return "A lighter tick on release, like a mechanical switch."
        case .middleDown:           return "Pressing the scroll wheel."
        case .scrollStep:           return "The ratchet detent. With Dynamic ratchet on, slow notches use this waveform and fast ones a lighter Step."
        case .screenEdge:           return "Once each time the pointer is pushed against a display edge."
        case .dragEnd:              return "Mouse up after a drag of more than 20 pt."
        case .windowSnap:           return "The focused window's frame changed to a tiling slot after a drag."
        case .appSwitch:            return "Another app became frontmost (⌘-Tab, Dock, click)."
        case .actionButton:         return "Whenever the action button is recognised."
        case .menuHighlight:        return "A tick as the highlighted item changes while sweeping a menu, the menu bar, or a right-click menu."
        case .textSelect:           return "A soft tick as the selection gains or loses a word, paced so a fast drag purrs instead of buzzing."
        case .textLine:             return "A slightly firmer notch each time the selection reaches another line."
        case .ringDetent:           return "Crossing from one Action Ring chamber to the next."
        case .ringOpen:             return "The ring appears under the pointer."
        case .ringCommit:           return "A chamber was chosen."
        case .freeSpinOn:           return "A flick of the wheel switches it to free-spin (MX SmartShift style)."
        case .freeSpinOff:          return "The wheel has come to rest after a free-spin and the notches are back."
        case .leftHold, .rightHold: return "Plays once the button has stayed down for the hold time: a swell under a long press, a second stage to the click."
        case .freeSpinTexture:      return "A faint tick every few notches while the wheel free-spins, so a fast slide still has grain."
        case .gestureStep:          return "Each volume or brightness step while the action button is held and the mouse moves up or down: tick, tick, tick."
        case .gestureLimit:         return "Volume already at the top or bottom: a bump instead of a step."
        case .gestureSwipe:         return "A sideways gesture ran its action (next track rises, previous track falls)."
        }
    }
    var permission: PermissionKind? {
        switch self {
        case .leftDown, .leftUp, .rightDown, .rightUp, .middleDown, .scrollStep, .actionButton: return .inputMonitoring
        case .screenEdge, .dragEnd: return .inputMonitoring
        case .windowSnap, .menuHighlight, .textSelect, .textLine: return .accessibility
        case .appSwitch, .ringDetent, .ringOpen, .ringCommit, .freeSpinOn, .freeSpinOff: return nil
        case .leftHold, .rightHold, .freeSpinTexture: return .inputMonitoring
        case .gestureStep, .gestureLimit, .gestureSwipe: return nil
        }
    }
    static let defaults: [HapticEvent: HapticRule] = [
        .leftDown:     HapticRule(enabled: false, ordinal: 10, intensity: 2),
        .leftUp:       HapticRule(enabled: false, ordinal: 3,  intensity: 1),
        .rightDown:    HapticRule(enabled: false, ordinal: 10, intensity: 2),
        .rightUp:      HapticRule(enabled: false, ordinal: 3,  intensity: 1),
        .middleDown:   HapticRule(enabled: true,  ordinal: 10, intensity: 3),
        .scrollStep:   HapticRule(enabled: false, ordinal: 6,  intensity: 2),
        .screenEdge:   HapticRule(enabled: true,  ordinal: 4,  intensity: 3),
        .dragEnd:      HapticRule(enabled: true,  ordinal: 5,  intensity: 3),
        .windowSnap:   HapticRule(enabled: true,  ordinal: 8,  intensity: 3),
        .appSwitch:    HapticRule(enabled: true,  ordinal: 3,  intensity: 2),
        .actionButton: HapticRule(enabled: true,  ordinal: 10, intensity: 4),
        .menuHighlight: HapticRule(enabled: true, ordinal: 6,  intensity: 2),   // STEP: a sharp 11 ms tick
        .textSelect:   HapticRule(enabled: true,  ordinal: 3,  intensity: 1),   // HOVER: the lightest 9 ms touch
        .textLine:     HapticRule(enabled: true,  ordinal: 5,  intensity: 2),   // ALIGN: a small notch per line
        .ringDetent:   HapticRule(enabled: true,  ordinal: 3,  intensity: 2),
        .ringOpen:     HapticRule(enabled: true,  ordinal: 7,  intensity: 2),
        .ringCommit:   HapticRule(enabled: true,  ordinal: 10, intensity: 4),
        .freeSpinOn:   HapticRule(enabled: true,  ordinal: 4,  intensity: 3),
        .freeSpinOff:  HapticRule(enabled: true,  ordinal: 5,  intensity: 2),
        .leftHold:     HapticRule(enabled: false, ordinal: 7,  intensity: 2),   // GROW: a 200 ms swell
        .rightHold:    HapticRule(enabled: false, ordinal: 7,  intensity: 2),
        .freeSpinTexture: HapticRule(enabled: false, ordinal: 3, intensity: 1),
        .gestureStep:  HapticRule(enabled: true,  ordinal: 6,  intensity: 2),   // STEP per volume notch
        .gestureLimit: HapticRule(enabled: true,  ordinal: 4,  intensity: 3),   // COLLIDE at the end of the range
        .gestureSwipe: HapticRule(enabled: true,  ordinal: 8,  intensity: 3),   // SUCCESS: an ascending pattern
    ]
}

struct HapticRule: Codable, Equatable {
    var enabled: Bool
    var ordinal: Int      // waveform ordinal in the device table
    var intensity: Int    // 1…4
    /// Optional second pulse after a delay: two-stage clicks, a spring-back on release.
    var echo: HapticEcho? = nil
}

struct HapticEcho: Codable, Equatable {
    var ordinal: Int
    var intensity: Int
    var delayMs: Int
}

enum ButtonGesture: String, CaseIterable, Codable, Identifiable {
    case press, doublePress, hold
    var id: String { rawValue }
    var title: String {
        switch self {
        case .press:       return "Press"
        case .doublePress: return "Double press"
        case .hold:        return "Press and hold"
        }
    }
}

/// What the action button does.
enum ButtonAction: Codable, Equatable {
    case none
    case actionRing
    case missionControl
    case spotlight
    case screenshot
    case lockScreen
    case playPause
    case showDesktop
    case launchApp(path: String)
    case shortcut(keyCode: Int, flags: UInt64, label: String)
    case shell(command: String)
    case gestures                                  // hold the button and move the mouse (see GestureController)
    case volumeUp, volumeDown, mute, nextTrack, previousTrack, brightnessUp, brightnessDown, spaceLeft, spaceRight

    enum Kind: String, CaseIterable, Identifiable {
        case none, actionRing, gestures, missionControl, spotlight, screenshot, lockScreen, playPause, showDesktop
        case volumeUp, volumeDown, mute, nextTrack, previousTrack, brightnessUp, brightnessDown, spaceLeft, spaceRight
        case launchApp, shortcut, shell
        var id: String { rawValue }
        var title: String {
            switch self {
            case .none:           return "Do nothing"
            case .actionRing:     return "Action Ring"
            case .gestures:       return "Hold-and-move gestures"
            case .volumeUp:       return "Volume up (step)"
            case .volumeDown:     return "Volume down (step)"
            case .mute:           return "Mute"
            case .nextTrack:      return "Next track"
            case .previousTrack:  return "Previous track"
            case .brightnessUp:   return "Brightness up (step)"
            case .brightnessDown: return "Brightness down (step)"
            case .spaceLeft:      return "Previous desktop"
            case .spaceRight:     return "Next desktop"
            case .missionControl: return "Mission Control"
            case .spotlight:      return "Spotlight"
            case .screenshot:     return "Screenshot toolbar"
            case .lockScreen:     return "Lock screen"
            case .playPause:      return "Play / Pause"
            case .showDesktop:    return "Show desktop"
            case .launchApp:      return "Open an app…"
            case .shortcut:       return "Keyboard shortcut…"
            case .shell:          return "Shell command…"
            }
        }
    }
    var kind: Kind {
        switch self {
        case .none: return .none
        case .actionRing: return .actionRing
        case .missionControl: return .missionControl
        case .spotlight: return .spotlight
        case .screenshot: return .screenshot
        case .lockScreen: return .lockScreen
        case .playPause: return .playPause
        case .showDesktop: return .showDesktop
        case .launchApp: return .launchApp
        case .gestures: return .gestures
        case .volumeUp: return .volumeUp
        case .volumeDown: return .volumeDown
        case .mute: return .mute
        case .nextTrack: return .nextTrack
        case .previousTrack: return .previousTrack
        case .brightnessUp: return .brightnessUp
        case .brightnessDown: return .brightnessDown
        case .spaceLeft: return .spaceLeft
        case .spaceRight: return .spaceRight
        case .shortcut: return .shortcut
        case .shell: return .shell
        }
    }
    static func blank(_ k: Kind) -> ButtonAction {
        switch k {
        case .none: return .none
        case .actionRing: return .actionRing
        case .missionControl: return .missionControl
        case .spotlight: return .spotlight
        case .screenshot: return .screenshot
        case .lockScreen: return .lockScreen
        case .playPause: return .playPause
        case .showDesktop: return .showDesktop
        case .launchApp: return .launchApp(path: "")
        case .gestures: return .gestures
        case .volumeUp: return .volumeUp
        case .volumeDown: return .volumeDown
        case .mute: return .mute
        case .nextTrack: return .nextTrack
        case .previousTrack: return .previousTrack
        case .brightnessUp: return .brightnessUp
        case .brightnessDown: return .brightnessDown
        case .spaceLeft: return .spaceLeft
        case .spaceRight: return .spaceRight
        case .shortcut: return .shortcut(keyCode: -1, flags: 0, label: "")
        case .shell: return .shell(command: "")
        }
    }
    var describe: String {
        switch self {
        case .launchApp(let p): return p.isEmpty ? "Open an app (none chosen)" : "Open " + ((p as NSString).lastPathComponent as NSString).deletingPathExtension
        case .shortcut(_, _, let l): return l.isEmpty ? "Keyboard shortcut (not recorded)" : "Press " + l
        case .shell(let c): return c.isEmpty ? "Shell command (empty)" : c
        default: return kind.title
        }
    }
    /// Stepped actions repeat every `stepPx` of gesture travel; the rest fire per swipe.
    var isStepped: Bool {
        switch self {
        case .volumeUp, .volumeDown, .brightnessUp, .brightnessDown: return true
        default: return false
        }
    }
}

/// One chamber of the Action Ring.
struct RingItem: Codable, Identifiable, Equatable {
    var id = UUID()
    var title: String
    var symbol: String          // SF Symbol name
    var action: ButtonAction

    static let defaults: [RingItem] = [
        RingItem(title: "Mission Control", symbol: "rectangle.3.group", action: .missionControl),
        RingItem(title: "Spotlight", symbol: "magnifyingglass", action: .spotlight),
        RingItem(title: "Screenshot", symbol: "camera.viewfinder", action: .screenshot),
        RingItem(title: "Play / Pause", symbol: "playpause.fill", action: .playPause),
        RingItem(title: "Show Desktop", symbol: "menubar.dock.rectangle", action: .showDesktop),
        RingItem(title: "Lock", symbol: "lock.fill", action: .lockScreen),
        RingItem(title: "Copy", symbol: "doc.on.doc", action: .shortcut(keyCode: 8, flags: CGEventFlags.maskCommand.rawValue, label: "⌘C")),
        RingItem(title: "Paste", symbol: "doc.on.clipboard", action: .shortcut(keyCode: 9, flags: CGEventFlags.maskCommand.rawValue, label: "⌘V")),
    ]
}

final class Settings: ObservableObject {
    // Master
    @Published var enabled = true { didSet { save() } }
    @Published var hapticsEnabled = true { didSet { save() } }
    @Published var masterIntensity = 4 { didSet { save() } }
    @Published var rules: [HapticEvent: HapticRule] = HapticEvent.defaults { didSet { save() } }
    @Published var scrollStepEvery = 1 { didSet { save() } }
    // Pointer & scroll (this mouse only)
    @Published var managePointer = false { didSet { save() } }
    @Published var pointerSpeed = 3.0 { didSet { save() } }        // System Settings scale 0…3
    @Published var noAcceleration = false { didSet { save() } }
    @Published var scrollSpeed = 0.3125 { didSet { save() } }      // macOS default for a wheel
    @Published var reverseScroll = false { didSet { save() } }
    // Wheel feel
    @Published var smoothScroll = true { didSet { save() } }
    @Published var scrollPixelsPerNotch = 60.0 { didSet { save() } }   // distance one notch travels
    @Published var scrollGlide = 5.0 { didSet { save() } }             // 1 = snappy … 10 = long glide
    @Published var scrollMomentum = 4.0 { didSet { save() } }          // 0 = none … 10 = long coast after a fast spin
    @Published var scrollAccel = 1.0 { didSet { save() } }             // 0 … 3: extra distance per notch when spinning fast
    @Published var freeSpin = true { didSet { save() } }
    @Published var ratchetDynamic = true { didSet { save() } }         // firm when slow, light when fast
    @Published var ratchetLightRate = 10.0 { didSet { save() } }      // notches per second where ticks go light
    @Published var freeSpinThreshold = 18.0 { didSet { save() } }      // notches per second
    @Published var freeSpinMultiplier = 3.0 { didSet { save() } }
    // Action Ring
    @Published var ringItems: [RingItem] = RingItem.defaults { didSet { save() } }
    // Action button
    @Published var actions: [ButtonGesture: ButtonAction] = [.press: .actionRing, .doublePress: .missionControl, .hold: .screenshot] { didSet { save() } }
    // Hold-and-move gestures (the action button held, the mouse moved)
    @Published var gestureUp: ButtonAction = .volumeUp { didSet { save() } }
    @Published var gestureDown: ButtonAction = .volumeDown { didSet { save() } }
    @Published var gestureLeft: ButtonAction = .previousTrack { didSet { save() } }
    @Published var gestureRight: ButtonAction = .nextTrack { didSet { save() } }
    @Published var gestureStepPx = 24.0 { didSet { save() } }        // pointer travel per volume / brightness step
    @Published var gestureSwipePx = 90.0 { didSet { save() } }       // pointer travel per swipe action
    @Published var gestureFreezePointer = true { didSet { save() } }
    /// Any hold (except the Action Ring) starts gesture mode; the hold's own action runs on release if the mouse did not move.
    @Published var gesturesOnHold = true { didSet { save() } }
    // Click feel extras
    @Published var clickHoldMs = 300 { didSet { save() } }           // when leftHold / rightHold play
    @Published var freeSpinTextureEvery = 2 { didSet { save() } }    // notches per texture tick during free-spin
    @Published var swallowButton = true { didSet { save() } }
    // Appearance
    @Published var theme: AppTheme = .system { didSet { save() } }
    @Published var accentHex: UInt = 0x0A84FF { didSet { save() } }
    var accent: Color { Color(rgb: accentHex) }

    func rule(_ e: HapticEvent) -> HapticRule { rules[e] ?? HapticEvent.defaults[e]! }
    func setRule(_ e: HapticEvent, _ r: HapticRule) { rules[e] = r }
    func binding(_ e: HapticEvent) -> Binding<HapticRule> {
        Binding(get: { self.rule(e) }, set: { self.rules[e] = $0 })
    }

    // MARK: Persistence (JSON in Application Support)

    private struct Snapshot: Codable {
        var enabled, hapticsEnabled: Bool
        var masterIntensity: Int
        var rules: RawMap<HapticRule>             // keyed by HapticEvent raw value; unknown keys are dropped
        var scrollStepEvery: Int
        var managePointer: Bool
        var pointerSpeed: Double
        var noAcceleration: Bool
        var scrollSpeed: Double
        var reverseScroll: Bool
        var smoothScroll: Bool?
        var scrollPixelsPerNotch: Double?
        var scrollGlide: Double?
        var scrollMomentum: Double?
        var scrollAccel: Double?
        var freeSpin: Bool?
        var freeSpinThreshold: Double?
        var freeSpinMultiplier: Double?
        var ratchetDynamic: Bool?
        var ratchetLightRate: Double?
        var ringItems: [RingItem]?
        var actions: RawMap<ButtonAction>         // keyed by ButtonGesture raw value
        var swallowButton: Bool
        var theme: AppTheme
        var accentHex: UInt
        var gestureUp, gestureDown, gestureLeft, gestureRight: ButtonAction?
        var gestureStepPx, gestureSwipePx: Double?
        var gestureFreezePointer: Bool?
        var gesturesOnHold: Bool?
        var clickHoldMs, freeSpinTextureEvery: Int?
    }

    static let url: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Thrum", isDirectory: true)
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base.appendingPathComponent("settings.json")
    }()

    private var loading = false
    private var pending: DispatchWorkItem?

    init() { load() }

    private func load() {
        guard let data = try? Data(contentsOf: Self.url) else { return }
        let s: Snapshot
        do { s = try JSONDecoder().decode(Snapshot.self, from: data) }
        catch {
            // Keep the unreadable file for inspection instead of silently overwriting it.
            let broken = Self.url.deletingPathExtension().appendingPathExtension("broken.json")
            try? FileManager.default.removeItem(at: broken)
            try? FileManager.default.copyItem(at: Self.url, to: broken)
            DebugLog.log("settings.json could not be decoded (\(error)); kept as settings.broken.json")
            return
        }
        loading = true
        enabled = s.enabled; hapticsEnabled = s.hapticsEnabled; masterIntensity = s.masterIntensity
        let savedRules = Dictionary(uniqueKeysWithValues: s.rules.dict.compactMap { k, v in HapticEvent(rawValue: k).map { ($0, v) } })
        rules = HapticEvent.defaults.merging(savedRules) { $1 }
        scrollStepEvery = s.scrollStepEvery
        managePointer = s.managePointer; pointerSpeed = s.pointerSpeed; noAcceleration = s.noAcceleration
        scrollSpeed = s.scrollSpeed; reverseScroll = s.reverseScroll
        smoothScroll = s.smoothScroll ?? true; scrollPixelsPerNotch = s.scrollPixelsPerNotch ?? 60
        scrollGlide = s.scrollGlide ?? 5; freeSpin = s.freeSpin ?? true
        scrollMomentum = s.scrollMomentum ?? 4; scrollAccel = s.scrollAccel ?? 1
        freeSpinThreshold = s.freeSpinThreshold ?? 18; freeSpinMultiplier = s.freeSpinMultiplier ?? 3
        ratchetDynamic = s.ratchetDynamic ?? true; ratchetLightRate = s.ratchetLightRate ?? 10
        ringItems = s.ringItems ?? RingItem.defaults
        actions = Dictionary(uniqueKeysWithValues: s.actions.dict.compactMap { k, v in ButtonGesture(rawValue: k).map { ($0, v) } })
        swallowButton = s.swallowButton
        theme = s.theme; accentHex = s.accentHex
        gestureUp = s.gestureUp ?? .volumeUp; gestureDown = s.gestureDown ?? .volumeDown
        gestureLeft = s.gestureLeft ?? .previousTrack; gestureRight = s.gestureRight ?? .nextTrack
        gestureStepPx = s.gestureStepPx ?? 24; gestureSwipePx = s.gestureSwipePx ?? 90
        gestureFreezePointer = s.gestureFreezePointer ?? true
        gesturesOnHold = s.gesturesOnHold ?? true
        clickHoldMs = s.clickHoldMs ?? 300; freeSpinTextureEvery = s.freeSpinTextureEvery ?? 2
        loading = false
    }

    private func save() {
        guard !loading else { return }
        pending?.cancel()
        let w = DispatchWorkItem { [weak self] in self?.writeNow() }
        pending = w
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: w)
    }

    /// Write any pending change now (quit path; saves are otherwise debounced).
    func flush() { pending?.cancel(); pending = nil; writeNow() }

    private func writeNow() {
        let s = Snapshot(enabled: enabled, hapticsEnabled: hapticsEnabled, masterIntensity: masterIntensity,
                         rules: RawMap(Dictionary(uniqueKeysWithValues: rules.map { ($0.key.rawValue, $0.value) })),
                         scrollStepEvery: scrollStepEvery, managePointer: managePointer,
                         pointerSpeed: pointerSpeed, noAcceleration: noAcceleration, scrollSpeed: scrollSpeed,
                         reverseScroll: reverseScroll, smoothScroll: smoothScroll,
                         scrollPixelsPerNotch: scrollPixelsPerNotch, scrollGlide: scrollGlide,
                         scrollMomentum: scrollMomentum, scrollAccel: scrollAccel, freeSpin: freeSpin,
                         freeSpinThreshold: freeSpinThreshold, freeSpinMultiplier: freeSpinMultiplier,
                         ratchetDynamic: ratchetDynamic, ratchetLightRate: ratchetLightRate,
                         ringItems: ringItems,
                         actions: RawMap(Dictionary(uniqueKeysWithValues: actions.map { ($0.key.rawValue, $0.value) })),
                         swallowButton: swallowButton,
                         theme: theme, accentHex: accentHex,
                         gestureUp: gestureUp, gestureDown: gestureDown, gestureLeft: gestureLeft, gestureRight: gestureRight,
                         gestureStepPx: gestureStepPx, gestureSwipePx: gestureSwipePx, gestureFreezePointer: gestureFreezePointer, gesturesOnHold: gesturesOnHold,
                         clickHoldMs: clickHoldMs, freeSpinTextureEvery: freeSpinTextureEvery)
        let enc = JSONEncoder(); enc.outputFormatting = [.prettyPrinted, .sortedKeys]
        if let data = try? enc.encode(s) { try? data.write(to: Self.url, options: .atomic) }
    }
}

/// A string-keyed map that also reads the layout Codable uses for enum-keyed
/// dictionaries ([key, value, key, value, …]), which older settings files have.
/// Unknown keys survive decoding and are filtered by the caller.
struct RawMap<V: Codable>: Codable {
    var dict: [String: V]
    init(_ d: [String: V]) { dict = d }
    init(from decoder: Decoder) throws {
        if let d = try? decoder.singleValueContainer().decode([String: V].self) { dict = d; return }
        var arr = try decoder.unkeyedContainer()
        var d: [String: V] = [:]
        while !arr.isAtEnd {
            let k = try arr.decode(String.self)
            d[k] = try arr.decode(V.self)
        }
        dict = d
    }
    func encode(to encoder: Encoder) throws {
        var c = encoder.singleValueContainer()
        try c.encode(dict)
    }
}

// MARK: - Feel presets
//
// A preset writes several rules at once; every row stays editable afterwards and
// the picker shows "Custom" until the rules match a preset again.
// Ordinals are the measured table: 3 HOVER 9 ms · 4 COLLIDE 26 · 5 ALIGN 9 · 6 STEP 11
// · 7 GROW 200 · 8 SUCCESS 70 · 9 ERROR 147 · 10 CLICK 11.

struct ClickFeel: Identifiable {
    let name: String
    let blurb: String
    let down: HapticRule
    let up: HapticRule
    let hold: HapticRule
    var id: String { name }

    private static func off(_ o: Int = 3) -> HapticRule { HapticRule(enabled: false, ordinal: o, intensity: 1) }
    private static func on(_ o: Int, _ i: Int, echo: HapticEcho? = nil) -> HapticRule { HapticRule(enabled: true, ordinal: o, intensity: i, echo: echo) }

    static let all: [ClickFeel] = [
        ClickFeel(name: "Firmware only", blurb: "No extra pulse; the switch and the firmware's own click.", down: off(10), up: off(), hold: off(7)),
        ClickFeel(name: "Subtle", blurb: "The lightest touch on press, nothing on release.", down: on(3, 1), up: off(), hold: off(7)),
        ClickFeel(name: "Soft", blurb: "A rounded press and a feather on release.", down: on(5, 2), up: on(3, 1), hold: off(7)),
        ClickFeel(name: "Crisp", blurb: "A sharp click with a faint release tick.", down: on(10, 2), up: on(3, 1), hold: off(7)),
        ClickFeel(name: "Mechanical", blurb: "Click-clack: a firm press and a distinct release, like a microswitch.", down: on(10, 3), up: on(6, 2), hold: off(7)),
        ClickFeel(name: "Deep", blurb: "A full-strength click with a low after-thud.", down: on(10, 4, echo: HapticEcho(ordinal: 4, intensity: 1, delayMs: 45)), up: on(5, 2), hold: off(7)),
        ClickFeel(name: "Push back", blurb: "Resists on the way down, springs back on release.", down: on(4, 3), up: on(5, 3, echo: HapticEcho(ordinal: 3, intensity: 1, delayMs: 55)), hold: off(7)),
        ClickFeel(name: "Double tap", blurb: "Two quick clicks per press.", down: on(10, 2, echo: HapticEcho(ordinal: 10, intensity: 2, delayMs: 65)), up: off(), hold: off(7)),
        ClickFeel(name: "Hold & swell", blurb: "A crisp click, then a slow swell if you keep the button down.", down: on(10, 2), up: on(3, 1), hold: on(7, 3)),
        ClickFeel(name: "Heavy hold", blurb: "A heavy press, a click on release, and a rising swell while held.", down: on(4, 4), up: on(6, 2), hold: on(7, 4, echo: HapticEcho(ordinal: 8, intensity: 2, delayMs: 230))),
    ]
}

struct WheelFeel: Identifiable {
    let name: String
    let blurb: String
    let step: HapticRule
    let every: Int
    let dynamic: Bool
    let lightRate: Double
    let freeSpin: Bool
    let texture: HapticRule
    let textureEvery: Int
    var id: String { name }

    private static func r(_ on: Bool, _ o: Int, _ i: Int, echo: HapticEcho? = nil) -> HapticRule { HapticRule(enabled: on, ordinal: o, intensity: i, echo: echo) }

    static let all: [WheelFeel] = [
        WheelFeel(name: "Classic ratchet", blurb: "A firm detent per notch that goes light when you spin fast; a flick free-spins silently.",
                  step: r(true, 6, 2), every: 1, dynamic: true, lightRate: 10, freeSpin: true, texture: r(false, 3, 1), textureEvery: 2),
        WheelFeel(name: "Fine", blurb: "Small, close ticks; the free-spin keeps a faint grain.",
                  step: r(true, 3, 2), every: 1, dynamic: true, lightRate: 14, freeSpin: true, texture: r(true, 3, 1), textureEvery: 2),
        WheelFeel(name: "Mechanical", blurb: "A click per notch, like a metal ratchet; lightens early when spinning.",
                  step: r(true, 10, 3), every: 1, dynamic: true, lightRate: 8, freeSpin: true, texture: r(false, 3, 1), textureEvery: 2),
        WheelFeel(name: "Heavy", blurb: "A thud per notch at any speed; no free-spin.",
                  step: r(true, 4, 3), every: 1, dynamic: false, lightRate: 10, freeSpin: false, texture: r(false, 3, 1), textureEvery: 2),
        WheelFeel(name: "Coarse", blurb: "A notch every second click of the wheel, with a coarse grain when free-spinning.",
                  step: r(true, 5, 3), every: 2, dynamic: false, lightRate: 10, freeSpin: true, texture: r(true, 6, 1), textureEvery: 3),
        WheelFeel(name: "Silk", blurb: "A whisper per notch and a continuous purr when the wheel slides.",
                  step: r(true, 3, 1), every: 1, dynamic: true, lightRate: 6, freeSpin: true, texture: r(true, 3, 1), textureEvery: 1),
        WheelFeel(name: "Slider", blurb: "Smooth slide feel: soft aligned bumps and an even texture, never a hard click.",
                  step: r(true, 5, 2), every: 1, dynamic: false, lightRate: 10, freeSpin: true, texture: r(true, 5, 1), textureEvery: 1),
        WheelFeel(name: "Rumble", blurb: "Each notch doubles up into a short rumble.",
                  step: r(true, 6, 3, echo: HapticEcho(ordinal: 3, intensity: 2, delayMs: 30)), every: 1, dynamic: false, lightRate: 10, freeSpin: true, texture: r(true, 3, 1), textureEvery: 2),
        WheelFeel(name: "Silent", blurb: "No wheel pulses at all; free-spin still boosts distance.",
                  step: r(false, 6, 2), every: 1, dynamic: true, lightRate: 10, freeSpin: true, texture: r(false, 3, 1), textureEvery: 2),
    ]
}

extension Settings {
    func applyClickFeel(_ f: ClickFeel, right: Bool) {
        rules[right ? .rightDown : .leftDown] = f.down
        rules[right ? .rightUp : .leftUp] = f.up
        rules[right ? .rightHold : .leftHold] = f.hold
    }
    func clickFeelName(right: Bool) -> String {
        ClickFeel.all.first { rule(right ? .rightDown : .leftDown) == $0.down && rule(right ? .rightUp : .leftUp) == $0.up && rule(right ? .rightHold : .leftHold) == $0.hold }?.name ?? "Custom"
    }
    func applyWheelFeel(_ f: WheelFeel) {
        rules[.scrollStep] = f.step; scrollStepEvery = f.every; ratchetDynamic = f.dynamic
        ratchetLightRate = f.lightRate; freeSpin = f.freeSpin
        rules[.freeSpinTexture] = f.texture; freeSpinTextureEvery = f.textureEvery
    }
    func wheelFeelName() -> String {
        WheelFeel.all.first { rule(.scrollStep) == $0.step && scrollStepEvery == $0.every && ratchetDynamic == $0.dynamic
            && ratchetLightRate == $0.lightRate && freeSpin == $0.freeSpin && rule(.freeSpinTexture) == $0.texture && freeSpinTextureEvery == $0.textureEvery }?.name ?? "Custom"
    }
}

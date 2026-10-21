import AppKit
import IOKit.hid
import Combine

enum PermissionKind: String, CaseIterable, Identifiable {
    case inputMonitoring, accessibility
    var id: String { rawValue }
    var title: String {
        switch self {
        case .inputMonitoring: return "Input Monitoring"
        case .accessibility:   return "Accessibility"
        }
    }
    var why: String {
        switch self {
        case .inputMonitoring:
            return "Lets iXpat read the mouse's own reports: button presses for click haptics, "
                 + "the scroll wheel, and the action button. Haptic playback itself works without it."
        case .accessibility:
            return "Needed to reverse scrolling for this mouse only, hide the action button's "
                 + "built-in shortcut from apps, detect drags and window tiling, and fire shortcuts."
        }
    }
    var settingsAnchor: String {
        switch self {
        case .inputMonitoring: return "Privacy_ListenEvent"
        case .accessibility:   return "Privacy_Accessibility"
        }
    }
    var tccService: String {
        switch self {
        case .inputMonitoring: return "ListenEvent"
        case .accessibility:   return "Accessibility"
        }
    }
}

final class Permissions: ObservableObject {
    @Published private(set) var inputMonitoring = false
    @Published private(set) var accessibility = false
    var onGranted: ((PermissionKind) -> Void)?
    private var timer: Timer?

    init() {
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in self?.refresh() }
    }

    func granted(_ kind: PermissionKind) -> Bool {
        kind == .accessibility ? accessibility : inputMonitoring
    }

    func refresh() {
        let im = IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted
        let ax = AXIsProcessTrusted()
        if im != inputMonitoring { inputMonitoring = im; if im { onGranted?(.inputMonitoring) } }
        if ax != accessibility   { accessibility = ax;   if ax { onGranted?(.accessibility) } }
    }

    /// First press raises the system prompt (macOS shows it once per app); a
    /// second press for the same permission opens the Settings pane instead.
    private var attempts: [PermissionKind: Int] = [:]
    func attempted(_ kind: PermissionKind) -> Bool {
        (attempts[kind] ?? 0) > 0 || (kind == .inputMonitoring && UserDefaults.standard.bool(forKey: "inputMonitoringRequested"))
    }
    func prompt(_ kind: PermissionKind) {
        objectWillChange.send()
        let n = (attempts[kind] ?? 0) + 1
        attempts[kind] = n
        switch kind {
        case .inputMonitoring:
            _ = IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)
        case .accessibility:
            let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
            _ = AXIsProcessTrustedWithOptions([key: true] as CFDictionary)
        }
        if n >= 2 { openSettings(kind) }
    }

    func openSettings(_ kind: PermissionKind) {
        guard let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?\(kind.settingsAnchor)") else { return }
        NSWorkspace.shared.open(url)
    }

    func resetCommand(_ kind: PermissionKind) -> String {
        "tccutil reset \(kind.tccService) \(Bundle.main.bundleIdentifier ?? "com.alcybr.ixpat")"
    }
}

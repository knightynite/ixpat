import AppKit
import ApplicationServices
import QuartzCore

/// Accessibility-driven haptic sources that need polling:
///  - the highlighted item of a tracked menu (menu bar, app menus, right-click menus)
///  - a growing text selection while the left button is held
///
/// Accessibility calls are synchronous IPC into other apps, so they run on a
/// background queue: never on the main thread (UI stalls) and never on the
/// event-tap thread (pointer stalls).
final class MenuWatcher {
    var onHighlight: (() -> Void)?
    enum SelectionStep { case word, line }
    var onSelectionGrow: ((SelectionStep) -> Void)?
    struct Config { var menuTicks = false; var textTicks = false; var controlTicks = false; var dropTicks = false }
    var onControl: (() -> Void)?
    var onDropTarget: (() -> Void)?
    var leftDown: () -> Bool = { false }
    /// Pushed from main whenever settings change; read on the AX queue.
    var config: Config { get { _config.value } set { _config.value = newValue } }
    private let _config = Locked(Config())
    /// Cheap gates evaluated on the AX queue (they read lock-protected values only).
    var pointerMovedRecently: () -> Bool = { false }
    var selectionGate: () -> Bool = { false }
    var debug = ProcessInfo.processInfo.environment["THRUM_DEBUG"] != nil   // logs the role of each hit, never titles

    private let systemWide = AXUIElementCreateSystemWide()
    private var lastKey = ""
    private var lastWords = -1
    private var lastLines = 0
    private var lastSelTick: CFTimeInterval = 0
    /// Pacing: a fast drag sweeps several words per poll; one soft tick per
    /// `wordSpacing` reads as texture, a tick per word reads as a buzz.
    private let wordSpacing: CFTimeInterval = 0.09
    private let lineSpacing: CFTimeInterval = 0.05
    private var timer: DispatchSourceTimer?
    private let queue = DispatchQueue(label: "com.alcybr.thrum.ax", qos: .utility)
    private static let roles: Set<String> = ["AXMenuItem", "AXMenuBarItem", "AXMenuButton"]
    /// Discrete controls worth one tick when the pointer arrives (Windows: hover over the Close button).
    private static let controlRoles: Set<String> = ["AXButton", "AXPopUpButton", "AXCheckBox", "AXRadioButton", "AXTab", "AXDockItem",
                                                    "AXLink", "AXDisclosureTriangle", "AXComboBox", "AXIncrementor", "AXColorWell", "AXSlider"]
    /// Things a drag can land on.
    private static let dropRoles: Set<String> = ["AXRow", "AXCell", "AXImage", "AXOutline", "AXTable", "AXList", "AXDockItem", "AXTextArea",
                                                 "AXTextField", "AXWebArea", "AXGroup", "AXButton", "AXTab", "AXScrollArea", "AXWindow"]
    private var lastControlKey = ""
    private var lastDropKey = ""
    private var lastControlTick: CFTimeInterval = 0

    func start() {
        guard timer == nil else { return }
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now(), repeating: .milliseconds(50), leeway: .milliseconds(10))
        t.setEventHandler { [weak self] in self?.tick() }
        t.resume()
        timer = t
    }

    func stop() { timer?.cancel(); timer = nil }

    private func attr(_ el: AXUIElement, _ name: String) -> CFTypeRef? {
        var v: CFTypeRef?
        return AXUIElementCopyAttributeValue(el, name as CFString, &v) == .success ? v : nil
    }
    private func role(_ el: AXUIElement) -> String { attr(el, kAXRoleAttribute) as? String ?? "" }
    private func title(_ el: AXUIElement) -> String { attr(el, kAXTitleAttribute) as? String ?? "" }

    /// Identity of a menu item that survives empty titles (Chromium/Electron menus
    /// expose untitled AXMenuItems): role, title, description, value and on-screen
    /// position. Position alone separates two items of the same menu.
    private func key(_ el: AXUIElement, _ r: String) -> String {
        var parts = [r, title(el)]
        if let d = attr(el, kAXDescriptionAttribute) as? String { parts.append(d) }
        if let v = attr(el, kAXValueAttribute) as? String { parts.append(v) }
        if let id = attr(el, kAXIdentifierAttribute) as? String { parts.append(id) }
        if let pv = attr(el, kAXPositionAttribute) {
            var pt = CGPoint.zero
            if AXValueGetValue(pv as! AXValue, .cgPoint, &pt) { parts.append("\(Int(pt.x)),\(Int(pt.y))") }
        }
        return parts.joined(separator: "|")
    }

    private func tick() {
        guard AXIsProcessTrusted() else { return }
        let c = config
        let moved = pointerMovedRecently()
        if c.menuTicks, moved { menuTick() }
        if c.textTicks, selectionGate() { selectionTick() } else { lastWords = -1 }
        let down = leftDown()
        if c.controlTicks, moved, !down { controlTick() } else { lastControlKey = "" }
        if c.dropTicks, moved, down { dropTick() } else { lastDropKey = "" }
    }

    // MARK: Menus

    private func menuTick() {
        var found: (String, String)? = nil    // (method, key)
        // Primary: hit-test the element under the pointer. Context menus (Chromium,
        // Electron, AppKit) are reachable this way even when they are not in the
        // app's child list and the focused element never changes.
        if let loc = CGEvent(source: nil)?.location {
            var hit: AXUIElement?
            if AXUIElementCopyElementAtPosition(systemWide, Float(loc.x), Float(loc.y), &hit) == .success, let el = hit {
                let r = role(el)
                if Self.roles.contains(r) {
                    let sel = attr(el, kAXSelectedAttribute) as? Bool ?? false
                    let t = title(el)
                    // Separators are untitled, unselected AXMenuItems; a bar item is only
                    // interesting once its menu is open (selected).
                    if sel || (!t.isEmpty && r != "AXMenuBarItem") { found = ("hit", key(el, r)) }
                }
            }
        }
        if found == nil, let f = attr(systemWide, kAXFocusedUIElementAttribute) {
            let el = f as! AXUIElement
            let r = role(el)
            if Self.roles.contains(r) { found = ("sys", key(el, r)) }
        }
        if found == nil, let app = NSWorkspace.shared.frontmostApplication {
            let ax = AXUIElementCreateApplication(app.processIdentifier)
            if let f = attr(ax, kAXFocusedUIElementAttribute) {
                let el = f as! AXUIElement
                let r = role(el)
                if Self.roles.contains(r) { found = ("app", key(el, r)) }
            }
            if found == nil, let kids = attr(ax, kAXChildrenAttribute) as? [AXUIElement] {
                for k in kids where role(k) == "AXMenu" {
                    if let items = attr(k, kAXChildrenAttribute) as? [AXUIElement] {
                        for it in items {
                            if let sel = attr(it, kAXSelectedAttribute) as? Bool, sel {
                                found = ("menu", key(it, "AXMenuItem")); break
                            }
                        }
                    }
                    if found != nil { break }
                }
            }
        }
        let key = found?.1 ?? ""
        guard key != lastKey else { return }
        let wasNone = lastKey.isEmpty
        lastKey = key
        guard !key.isEmpty else { return }
        if debug { DebugLog.log("menu highlight via \(found!.0): \(key.split(separator: "|").first ?? "")") }
        // Clicking a menu-bar title highlights it; that is the click, not a move.
        if wasNone, key.hasPrefix("AXMenuBarItem") { return }
        DispatchQueue.main.async { [weak self] in self?.onHighlight?() }
    }

    // MARK: Controls under the pointer / drop targets under a drag

    private func hitUnderPointer() -> (AXUIElement, String)? {
        guard let loc = CGEvent(source: nil)?.location else { return nil }
        var hit: AXUIElement?
        guard AXUIElementCopyElementAtPosition(systemWide, Float(loc.x), Float(loc.y), &hit) == .success, let el = hit else { return nil }
        return (el, role(el))
    }

    private func controlTick() {
        guard let (el, r) = hitUnderPointer() else { return }
        let k = Self.controlRoles.contains(r) && (attr(el, kAXEnabledAttribute) as? Bool ?? true) ? key(el, r) : ""
        guard k != lastControlKey else { return }
        lastControlKey = k
        guard !k.isEmpty else { return }
        let now = CACurrentMediaTime()
        guard now - lastControlTick >= 0.06 else { return }
        lastControlTick = now
        DispatchQueue.main.async { [weak self] in self?.onControl?() }
    }

    private func dropTick() {
        guard let (el, r) = hitUnderPointer() else { return }
        let k = Self.dropRoles.contains(r) ? key(el, r) : ""
        let first = lastDropKey.isEmpty
        guard k != lastDropKey else { return }
        lastDropKey = k
        guard !k.isEmpty, !first else { return }          // the element the drag started on is not a target
        let now = CACurrentMediaTime()
        guard now - lastControlTick >= 0.08 else { return }
        lastControlTick = now
        DispatchQueue.main.async { [weak self] in self?.onDropTarget?() }
    }

    // MARK: Text selection (while dragging)

    private func selectionTick() {
        guard let app = NSWorkspace.shared.frontmostApplication else { return }
        let ax = AXUIElementCreateApplication(app.processIdentifier)
        guard let f = attr(ax, kAXFocusedUIElementAttribute) else { return }
        let el = f as! AXUIElement
        guard let text = attr(el, kAXSelectedTextAttribute) as? String else { return }
        let words = text.split(whereSeparator: { $0.isWhitespace || $0.isNewline }).count
        let lines = text.reduce(0) { $0 + ($1.isNewline ? 1 : 0) }
        if lastWords < 0 { lastWords = words; lastLines = lines; return }   // first sample after the press
        guard words != lastWords || lines != lastLines else { return }
        let crossedLine = lines != lastLines
        lastWords = words; lastLines = lines
        let now = CACurrentMediaTime()
        let step: SelectionStep = crossedLine ? .line : .word
        let spacing = crossedLine ? lineSpacing : wordSpacing
        guard now - lastSelTick >= spacing else { return }
        lastSelTick = now
        DispatchQueue.main.async { [weak self] in self?.onSelectionGrow?(step) }
    }
}

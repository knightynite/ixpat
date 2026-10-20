import AppKit
import ApplicationServices
import QuartzCore

/// Accessibility notifications from the frontmost app, for the controls Windows
/// gives haptics to: slider detents (Step per tick), selection changes in tab
/// groups and collection lists (PowerPoint's thumbnail pane, Safari's tabs),
/// and the scroll bar reaching either end (an overscroll bump).
///
/// The observer lives on the main run loop (AXObserver needs one); element
/// reads happen on a utility queue so the main thread never blocks on IPC.
final class AXWatcher {
    struct Config { var sliderTicks = false; var selectionTicks = false; var scrollEndTicks = false }
    var config: Config { get { _config.value } set { _config.value = newValue } }
    private let _config = Locked(Config())
    /// Pointer-coupled gate: only react within a short time of mouse activity.
    var pointerActiveRecently: () -> Bool = { false }
    var onSlider: (() -> Void)?
    var onSelection: (() -> Void)?
    var onScrollEnd: (() -> Void)?

    private var observer: AXObserver?
    private var observedPid: pid_t = 0
    private let queue = DispatchQueue(label: "com.alcybr.ixpat.ax-notify", qos: .utility)
    private var sliderBuckets: [String: Int] = [:]      // keyed by element description, on `queue`
    private var atEnd: [String: Bool] = [:]
    private var lastTick: CFTimeInterval = 0

    private static let notifications: [String] = [
        kAXValueChangedNotification,
        kAXSelectedChildrenChangedNotification,
        kAXSelectedRowsChangedNotification,
    ]

    /// Main thread. Re-point the observer at the app that just became frontmost.
    func observe(pid: pid_t) {
        guard pid != observedPid, pid != ProcessInfo.processInfo.processIdentifier, AXIsProcessTrusted() else { return }
        detach()
        var obs: AXObserver?
        let cb: AXObserverCallback = { _, element, notification, refcon in
            guard let refcon else { return }
            let me = Unmanaged<AXWatcher>.fromOpaque(refcon).takeUnretainedValue()
            me.handle(element, notification as String)
        }
        guard AXObserverCreate(pid, cb, &obs) == .success, let obs else { return }
        let app = AXUIElementCreateApplication(pid)
        let refcon = Unmanaged.passUnretained(self).toOpaque()
        for n in Self.notifications { AXObserverAddNotification(obs, app, n as CFString, refcon) }
        CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(obs), .defaultMode)
        observer = obs; observedPid = pid
        queue.async { [weak self] in self?.sliderBuckets = [:]; self?.atEnd = [:] }
    }

    func detach() {
        if let observer {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer), .defaultMode)
        }
        observer = nil; observedPid = 0
    }

    private func attr(_ el: AXUIElement, _ name: String) -> CFTypeRef? {
        var v: CFTypeRef?
        return AXUIElementCopyAttributeValue(el, name as CFString, &v) == .success ? v : nil
    }
    private func number(_ v: CFTypeRef?) -> Double? {
        if let n = v as? NSNumber, n.doubleValue.isFinite { return n.doubleValue }
        return nil
    }
    /// Another app's accessibility values are untrusted: never let them trap an Int conversion.
    private func safeInt(_ d: Double) -> Int { d.isFinite ? Int(max(min(d, 1e9), -1e9)) : 0 }
    /// Keys are role + position, so a long session in one app can keep adding them: cap the maps.
    private func trim<V>(_ m: inout [String: V]) { if m.count > 512 { m.removeAll() } }
    private func ident(_ el: AXUIElement) -> String {
        var pt = CGPoint.zero
        if let pv = attr(el, kAXPositionAttribute), CFGetTypeID(pv) == AXValueGetTypeID() { AXValueGetValue(pv as! AXValue, .cgPoint, &pt) }
        return "\(attr(el, kAXRoleAttribute) as? String ?? "")|\(safeInt(pt.x)),\(safeInt(pt.y))"
    }

    /// Main thread (observer callback): hop off before touching the element.
    private func handle(_ el: AXUIElement, _ notification: String) {
        let c = config
        guard c.sliderTicks || c.selectionTicks || c.scrollEndTicks, pointerActiveRecently() else { return }
        queue.async { [weak self] in self?.process(el, notification, c) }
    }

    private func process(_ el: AXUIElement, _ notification: String, _ c: Config) {
        let role = attr(el, kAXRoleAttribute) as? String ?? ""
        let now = CACurrentMediaTime()
        switch notification {
        case kAXValueChangedNotification:
            guard let v = number(attr(el, kAXValueAttribute)) else { return }
            if role == "AXSlider", c.sliderTicks {
                // 20 detents across the range, like Windows' slider guidance.
                let lo = number(attr(el, kAXMinValueAttribute)) ?? 0
                let hi = number(attr(el, kAXMaxValueAttribute)) ?? 1
                guard hi > lo else { return }
                let bucket = safeInt(((v - lo) / (hi - lo) * 20).rounded(.down))
                let k = ident(el)
                trim(&sliderBuckets)
                if let prev = sliderBuckets[k], prev != bucket, now - lastTick >= 0.04 {
                    lastTick = now
                    DispatchQueue.main.async { [weak self] in self?.onSlider?() }
                }
                sliderBuckets[k] = bucket
            } else if role == "AXScrollBar", c.scrollEndTicks {
                // Scroll bar value is 0…1; reaching either end is the overscroll bump.
                let k = ident(el)
                trim(&atEnd)
                let end = v <= 0.0005 || v >= 0.9995
                if end, atEnd[k] != true, now - lastTick >= 0.15 {
                    lastTick = now
                    DispatchQueue.main.async { [weak self] in self?.onScrollEnd?() }
                }
                atEnd[k] = end
            }
        case kAXSelectedChildrenChangedNotification, kAXSelectedRowsChangedNotification:
            guard c.selectionTicks else { return }
            // Tab groups, segmented controls, collection lists; not text views or tables scrolling by.
            let wanted: Set<String> = ["AXTabGroup", "AXRadioGroup", "AXList", "AXLayoutArea", "AXToolbar", "AXOutline", "AXTable", "AXGroup"]
            guard wanted.contains(role), now - lastTick >= 0.1 else { return }
            lastTick = now
            DispatchQueue.main.async { [weak self] in self?.onSelection?() }
        default: break
        }
    }
}

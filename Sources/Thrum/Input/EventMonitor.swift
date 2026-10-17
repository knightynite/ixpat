import AppKit
import ApplicationServices
import QuartzCore

struct SmoothConfig {
    var enabled: Bool
    var pxPerNotch: Double
    var glide: Double        // 1…10: how long a single slow notch takes to settle
    var momentum: Double     // 0…10: how far a fast spin coasts after the wheel stops (0 = none)
    var accel: Double        // 0…3: extra distance per notch when spinning fast
    var multiplier: Double   // free-spin boost
    var reverse: Bool
    static let off = SmoothConfig(enabled: false, pxPerNotch: 60, glide: 5, momentum: 4, accel: 1, multiplier: 1, reverse: false)
}

/// One CGEvent tap for everything that needs the system event stream:
/// screen-edge and drag detection, smooth / reversed scrolling for the wheel,
/// window-tiling detection, Action Ring clicks, and hiding the action button's
/// built-in shortcut.
///
/// With Accessibility it is an active tap (can change or drop events). With
/// only Input Monitoring it falls back to listen-only, which still feeds the
/// haptic events but cannot change scrolling or swallow anything.
///
/// Threads: `start`/`stop` and the screen cache run on main; `handle` and the
/// scroll timer run on the tap thread; window-frame accessibility calls run on
/// a utility queue. Everything crossing between them goes through `Locked`.
final class EventMonitor {
    /// Fired from the tap thread or the AX queue; the receiver hops to main.
    var onEvent: ((HapticEvent) -> Void)?
    var shouldSwallow: ((CGEvent, CGEventType) -> Bool)?
    /// Ring open and a left click arrived: return true to consume it (commit).
    var ringClick: (() -> Bool)?
    /// Ring open and a right click arrived: return true to consume it (cancel).
    var ringCancel: (() -> Bool)?
    /// Pointer deltas while the action button is held: return true to consume the move (gesture mode).
    var gestureMove: ((Double, Double) -> Bool)?

    private(set) var running = false
    private(set) var listenOnly = false
    /// Kinetic scrolling parameters, pushed from main whenever settings change.
    var smooth: SmoothConfig { get { _smooth.value } set { _smooth.value = newValue } }
    private let _smooth = Locked(SmoothConfig.off)
    var lastPointerMove: CFTimeInterval { _lastMove.value }
    private let _lastMove = Locked<CFTimeInterval>(0)
    var lastClick: CFTimeInterval { _lastClick.value }
    private let _lastClick = Locked<CFTimeInterval>(0)
    /// True between a left press and its release (used by the selection watcher).
    var leftButtonDown: Bool { _leftDown.value }
    private let _leftDown = Locked(false)
    private var tap: CFMachPort?
    private var source: CFRunLoopSource?

    private var dragStart = CGPoint.zero
    private var dragDistance: CGFloat = 0
    private let frameBeforeDrag = Locked<CGRect?>(nil)
    private var atEdge = false
    /// A swallowed press hides its release too, but only if it arrives soon:
    /// a release lost to a tap restart must not eat the next real one.
    private var swallowLeftUpUntil: CFTimeInterval = 0
    private var swallowRightUpUntil: CFTimeInterval = 0
    private let axQueue = DispatchQueue(label: "com.alcybr.thrum.ax-window", qos: .utility)
    /// Display frames in CG coordinates (top-left origin), cached on main for the tap thread.
    private let screens = Locked<(frames: [CGRect], visible: [CGRect])>(([], []))
    private var screenObserver: Any?

    // Smooth scrolling
    private static let ownTag: Int64 = 0x5448524D   // "THRM": marks events we synthesise
    private lazy var scrollSource: CGEventSource? = {
        let s = CGEventSource(stateID: .hidSystemState)
        s?.userData = Self.ownTag
        return s
    }()
    // Kinetic scrolling state (tap thread only): velocity in px/s per axis, sub-pixel carry,
    // the current friction time constant, and the recent notch times for the wheel rate.
    private var velX = 0.0, velY = 0.0, carryX = 0.0, carryY = 0.0
    private var tau = 0.05
    private var notchTimes: [CFTimeInterval] = []
    private var scrollTimer: CFRunLoopTimer?
    private var lastTick: CFTimeInterval = 0

    func start() {
        guard tap == nil else { return }
        refreshScreens()
        if screenObserver == nil {
            screenObserver = NotificationCenter.default.addObserver(forName: NSApplication.didChangeScreenParametersNotification,
                                                                    object: nil, queue: .main) { [weak self] _ in self?.refreshScreens() }
        }
        let types: [CGEventType] = [.leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp,
                                    .otherMouseDown, .otherMouseUp, .otherMouseDragged, .mouseMoved, .scrollWheel,
                                    .keyDown, .keyUp, .flagsChanged]
        var mask: CGEventMask = 0
        for t in types { mask |= 1 << t.rawValue }
        mask |= 1 << 14   // NX_SYSDEFINED (media / consumer keys)
        let refcon = Unmanaged.passUnretained(self).toOpaque()
        var created = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .headInsertEventTap, options: .defaultTap,
                                        eventsOfInterest: mask, callback: eventTapCallback, userInfo: refcon)
        listenOnly = false
        if created == nil {
            created = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .headInsertEventTap, options: .listenOnly,
                                        eventsOfInterest: mask, callback: eventTapCallback, userInfo: refcon)
            listenOnly = true
        }
        guard let created else { running = false; return }
        tap = created
        source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, created, 0)
        CFRunLoopAddSource(TapThread.shared.runLoop, source, .commonModes)
        CGEvent.tapEnable(tap: created, enable: true)
        running = true
    }

    func stop() {
        if let source { CFRunLoopRemoveSource(TapThread.shared.runLoop, source, .commonModes) }
        if let tap { CGEvent.tapEnable(tap: tap, enable: false); CFMachPortInvalidate(tap) }
        source = nil; tap = nil; running = false
        // Scroll state belongs to the tap thread: reset it there.
        let rl = TapThread.shared.runLoop!
        CFRunLoopPerformBlock(rl, CFRunLoopMode.commonModes.rawValue) { [weak self] in self?.resetTapState() }
        CFRunLoopWakeUp(rl)
    }

    /// Tap thread. Drops any coast in flight and forgets pending swallows.
    private func resetTapState() {
        velX = 0; velY = 0; carryX = 0; carryY = 0; notchTimes = []
        swallowLeftUpUntil = 0; swallowRightUpUntil = 0
        if let scrollTimer { CFRunLoopTimerInvalidate(scrollTimer) }
        scrollTimer = nil
    }

    /// Main thread. AppKit screen geometry, converted once for the tap thread.
    private func refreshScreens() {
        let mainH = NSScreen.screens.first?.frame.height ?? 0
        func cg(_ r: CGRect) -> CGRect { CGRect(x: r.minX, y: mainH - r.maxY, width: r.width, height: r.height) }
        screens.value = (NSScreen.screens.map { cg($0.frame) }, NSScreen.screens.map { cg($0.visibleFrame) })
    }

    /// Re-create the tap (e.g. after Accessibility was granted, to upgrade from listen-only).
    func restart() { stop(); start() }

    fileprivate func reEnable() { if let tap { CGEvent.tapEnable(tap: tap, enable: true) } }

    /// Runs on the tap thread. Returns nil to drop the event, or the (possibly modified) event.
    fileprivate func handle(_ type: CGEventType, _ event: CGEvent) -> CGEvent? {
        let now = CACurrentMediaTime()
        switch type {
        case .leftMouseDown, .rightMouseDown, .keyDown: velX = 0; velY = 0   // a click or key stops the coast
        default: break
        }
        if type == .mouseMoved || type == .leftMouseDragged || type == .otherMouseDragged, let g = gestureMove,
           g(Double(event.getIntegerValueField(.mouseEventDeltaX)), Double(event.getIntegerValueField(.mouseEventDeltaY))) {
            return listenOnly ? event : nil
        }
        switch type {
        case .leftMouseDown:
            if !listenOnly, ringClick?() == true { swallowLeftUpUntil = now + 1.5; return nil }
            if !listenOnly, shouldSwallow?(event, type) == true { swallowLeftUpUntil = now + 1.5; return nil }
            dragStart = event.location; dragDistance = 0
            _leftDown.value = true
            _lastClick.value = now
            // Accessibility IPC must not run on the tap thread (or main): snapshot the window frame off both.
            axQueue.async { [weak self] in guard let self else { return }; self.frameBeforeDrag.value = self.focusedWindowFrame() }
        case .leftMouseDragged:
            _lastMove.value = now
            let p = event.location
            dragDistance += hypot(p.x - dragStart.x, p.y - dragStart.y); dragStart = p
            checkEdge(p)
        case .leftMouseUp:
            _leftDown.value = false
            if now < swallowLeftUpUntil { swallowLeftUpUntil = 0; return nil }
            if dragDistance > 20 {
                onEvent?(.dragEnd)
                let before = frameBeforeDrag.value
                axQueue.asyncAfter(deadline: .now() + 0.4) { [weak self] in self?.checkSnap(before: before) }
            }
            dragDistance = 0
        case .rightMouseDown:
            if !listenOnly, ringCancel?() == true { swallowRightUpUntil = now + 1.5; return nil }
        case .rightMouseUp:
            if now < swallowRightUpUntil { swallowRightUpUntil = 0; return nil }
        case .mouseMoved, .otherMouseDragged:
            _lastMove.value = now
            checkEdge(event.location)
        case .scrollWheel:
            if event.getIntegerValueField(.eventSourceUserData) == Self.ownTag { return event }   // ours
            // Wheel mice report non-continuous scrolls; trackpads and Magic Mouse are continuous.
            guard !listenOnly, event.getIntegerValueField(.scrollWheelEventIsContinuous) == 0 else { break }
            let cfg = smooth
            if cfg.enabled {
                kineticNotch(event, cfg)
                return nil
            } else if cfg.reverse {
                for f in [CGEventField.scrollWheelEventDeltaAxis1, .scrollWheelEventDeltaAxis2,
                          .scrollWheelEventPointDeltaAxis1, .scrollWheelEventPointDeltaAxis2] {
                    event.setIntegerValueField(f, value: -event.getIntegerValueField(f))
                }
                for f in [CGEventField.scrollWheelEventFixedPtDeltaAxis1, .scrollWheelEventFixedPtDeltaAxis2] {
                    event.setDoubleValueField(f, value: -event.getDoubleValueField(f))
                }
            }
        case .keyDown, .keyUp, .flagsChanged, .otherMouseDown, .otherMouseUp:
            if !listenOnly, shouldSwallow?(event, type) == true { return nil }
        default:
            if type.rawValue == 14, !listenOnly, shouldSwallow?(event, type) == true { return nil }
        }
        return event
    }

    // MARK: Kinetic scrolling (Magic Mouse / Windows precision feel for a notched wheel)
    //
    // Every notch is a velocity impulse; the velocity decays with a friction time
    // constant `tau` that follows how fast the wheel is spinning. A lone slow notch
    // uses a short tau and travels exactly pxPerNotch, so it settles crisply; a fast
    // spin stretches tau so the content keeps coasting after the wheel stops, and an
    // acceleration gain makes each of those notches worth more pixels.

    private func kineticNotch(_ event: CGEvent, _ cfg: SmoothConfig) {
        let now = CACurrentMediaTime()
        notchTimes.append(now)
        notchTimes.removeAll { now - $0 > 0.25 }
        let rate = Double(notchTimes.count) / 0.25                       // notches per second
        let fast = min(1, max(0, (rate - 5) / 20))                       // 0 at ≤5/s … 1 at ≥25/s
        let tauSlow = 0.015 + cfg.glide * 0.008                          // 0.023 … 0.095 s
        let tauFast = max(tauSlow, cfg.momentum * 0.12)                  // up to 1.2 s of coast
        tau = tauSlow + (tauFast - tauSlow) * fast
        let gain = (1 + cfg.accel * fast) * cfg.multiplier
        let sign = cfg.reverse ? -1.0 : 1.0
        func notches(_ f: CGEventField) -> Double {
            let v = Double(event.getIntegerValueField(f))
            return v == 0 ? 0 : (v > 0 ? min(v, 2) : max(v, -2))         // cap the OS line ramp; our gain does the rest
        }
        // Distance of an impulse Δv under exponential decay is Δv·tau, so Δv = px/tau.
        func kick(_ vel: inout Double, _ n: Double) {
            guard n != 0 else { return }
            var d = n * cfg.pxPerNotch * gain * sign
            if vel != 0, (d > 0) != (vel > 0) {
                // Reversing kills the coast and starts a fresh, slow gesture: one plain notch.
                vel = 0
                notchTimes = [now]
                tau = tauSlow
                d = n * cfg.pxPerNotch * cfg.multiplier * sign
            }
            vel += d / tau
        }
        kick(&velY, notches(.scrollWheelEventDeltaAxis1))
        kick(&velX, notches(.scrollWheelEventDeltaAxis2))
        ensureScrollTimer()
    }

    /// The scroll timer lives on the tap thread too, so scroll state is only
    /// ever touched from one thread.
    private func ensureScrollTimer() {
        guard scrollTimer == nil else { return }
        let t = CFRunLoopTimerCreateWithHandler(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 1.0 / 120, 1.0 / 120, 0, 0) { [weak self] _ in
            self?.scrollTick()
        }
        CFRunLoopAddTimer(TapThread.shared.runLoop, t, .commonModes)
        scrollTimer = t
        lastTick = 0
    }

    private func scrollTick() {
        // Integrate over the real elapsed time, so a late timer does not slow the coast.
        let now = CACurrentMediaTime()
        let dt = lastTick > 0 ? min(max(now - lastTick, 1.0 / 240), 0.05) : 1.0 / 120
        lastTick = now
        if abs(velX) < 40 && abs(velY) < 40 {                           // below ~1/3 px per frame: done
            velX = 0; velY = 0; carryX = 0; carryY = 0
            if let scrollTimer { CFRunLoopTimerInvalidate(scrollTimer) }
            scrollTimer = nil
            return
        }
        let decay = exp(-dt / tau)
        func step(_ vel: inout Double, _ carry: inout Double) -> Int32 {
            guard vel != 0 else { return 0 }
            let s = vel * tau * (1 - decay)                              // exact integral over the frame
            vel *= decay
            let total = s + carry
            let whole = total.rounded(.towardZero)
            carry = total - whole
            return Int32(whole)
        }
        let dy = step(&velY, &carryY), dx = step(&velX, &carryX)
        guard dy != 0 || dx != 0 else { return }
        guard let ev = CGEvent(scrollWheelEvent2Source: scrollSource, units: .pixel, wheelCount: 2,
                               wheel1: dy, wheel2: dx, wheel3: 0) else { return }
        ev.setIntegerValueField(.scrollWheelEventIsContinuous, value: 1)
        ev.post(tap: .cghidEventTap)
    }

    // MARK: Screen edge

    /// Tap thread; uses the cached CG-space display frames.
    private func checkEdge(_ p: CGPoint) {
        guard let f = screens.value.frames.first(where: { $0.insetBy(dx: -1, dy: -1).contains(p) }) else { return }
        let hit = p.x <= f.minX + 0.5 || p.x >= f.maxX - 1.5 || p.y <= f.minY + 0.5 || p.y >= f.maxY - 1.5
        if hit != atEdge {
            atEdge = hit
            if hit { onEvent?(.screenEdge) }
        }
    }

    // MARK: Window tiling (Accessibility, on the utility queue)

    private func focusedWindowFrame() -> CGRect? {
        guard AXIsProcessTrusted(), let app = NSWorkspace.shared.frontmostApplication else { return nil }
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        var winRef: CFTypeRef?
        guard AXUIElementCopyAttributeValue(axApp, kAXFocusedWindowAttribute as CFString, &winRef) == .success,
              let winRef else { return nil }
        let win = winRef as! AXUIElement
        var posRef: CFTypeRef?, sizeRef: CFTypeRef?
        guard AXUIElementCopyAttributeValue(win, kAXPositionAttribute as CFString, &posRef) == .success,
              AXUIElementCopyAttributeValue(win, kAXSizeAttribute as CFString, &sizeRef) == .success,
              let posRef, let sizeRef else { return nil }
        var pos = CGPoint.zero, size = CGSize.zero
        AXValueGetValue(posRef as! AXValue, .cgPoint, &pos)
        AXValueGetValue(sizeRef as! AXValue, .cgSize, &size)
        return CGRect(origin: pos, size: size)
    }

    private func checkSnap(before: CGRect?) {
        guard let now = focusedWindowFrame() else { return }
        if let before, abs(before.minX - now.minX) < 4, abs(before.minY - now.minY) < 4,
           abs(before.width - now.width) < 4, abs(before.height - now.height) < 4 { return }   // did not move
        for v in screens.value.visible {
            let halfW = v.width / 2, halfH = v.height / 2
            let slots = [
                v,
                CGRect(x: v.minX, y: v.minY, width: halfW, height: v.height),
                CGRect(x: v.midX, y: v.minY, width: halfW, height: v.height),
                CGRect(x: v.minX, y: v.minY, width: v.width, height: halfH),
                CGRect(x: v.minX, y: v.midY, width: v.width, height: halfH),
                CGRect(x: v.minX, y: v.minY, width: halfW, height: halfH),
                CGRect(x: v.midX, y: v.minY, width: halfW, height: halfH),
                CGRect(x: v.minX, y: v.midY, width: halfW, height: halfH),
                CGRect(x: v.midX, y: v.midY, width: halfW, height: halfH),
            ]
            for s in slots where abs(s.minX - now.minX) < 12 && abs(s.minY - now.minY) < 12
                                && abs(s.width - now.width) < 24 && abs(s.height - now.height) < 24 {
                onEvent?(.windowSnap)
                return
            }
        }
    }
}

private func eventTapCallback(proxy: CGEventTapProxy, type: CGEventType, event: CGEvent,
                              refcon: UnsafeMutableRawPointer?) -> Unmanaged<CGEvent>? {
    guard let refcon else { return Unmanaged.passUnretained(event) }
    let monitor = Unmanaged<EventMonitor>.fromOpaque(refcon).takeUnretainedValue()
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        monitor.reEnable()
        return Unmanaged.passUnretained(event)
    }
    guard let out = monitor.handle(type, event) else { return nil }
    return Unmanaged.passUnretained(out)
}

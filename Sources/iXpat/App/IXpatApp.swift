import SwiftUI
import AppKit
import Combine
import QuartzCore
import IOKit.hid

@main
struct IXpatApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    var body: some Scene {
        MenuBarExtra {
            MenuView()
                .environmentObject(delegate.settings)
                .environmentObject(delegate.device)
                .environmentObject(delegate.engine)
                .environmentObject(delegate.permissions)
        } label: {
            MenuBarLabel()
        }
        .menuBarExtraStyle(.window)

    }
}

struct MenuBarLabel: View {
    var body: some View { Image(systemName: "computermouse.fill") }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    /// Views reach the delegate here: under the SwiftUI lifecycle `NSApp.delegate` is
    /// SwiftUI's own forwarding object, so `NSApp.delegate as? AppDelegate` is nil.
    private(set) static weak var shared: AppDelegate?
    override init() { super.init(); AppDelegate.shared = self }
    let settings = Settings()
    let device = MouseDevice()
    let engine = HapticEngine()
    let permissions = Permissions()
    let monitor = EventMonitor()
    let menus = MenuWatcher()
    let ring = RadialMenuController.shared
    let button = ActionButtonController()
    let gestures = GestureController()
    let hover = HoverWatcher()
    let axWatch = AXWatcher()
    let bridge = HapticBridge()

    // Wheel feel: notch rate → free-spin
    private var notchTimes: [CFTimeInterval] = []
    private var freeSpinActive = false
    private var freeSpinTimer: Timer?

    private var settingsWindow: NSWindow?
    private var aboutWindow: NSWindow?
    private var menuPreview: NSWindow?
    private var bag = Set<AnyCancellable>()
    private var wheelAccum = 0
    private var lastNotchTime: CFTimeInterval = 0
    private var lastNotchDir = 0
    private var lastRatchetTick: CFTimeInterval = 0
    private var textureAccum = 0
    private var holdTimers: [UInt8: Timer] = [:]
    /// The hold action deferred to release while gesture mode runs (runs only if nothing was gestured).
    private var pendingHoldAction: ButtonAction?

    /// The settings window is a plain NSWindow hosting the SwiftUI view, so it
    /// can be opened from anywhere (first launch, Finder reopen, the menu) even
    /// when the status item is hidden by a full menu bar or a notch.
    @MainActor @discardableResult
    func showSettings() -> Bool {
        DebugLog.log("showSettings existing=\(settingsWindow != nil) windows=\(NSApp.windows.filter { $0.isVisible }.map { String(describing: type(of: $0)) })")
        if settingsWindow == nil {
            let root = MainWindow()
                .environmentObject(settings).environmentObject(device).environmentObject(engine)
                .environmentObject(permissions).environmentObject(button)
            let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 860, height: 700),
                             styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
            w.title = "iXpat"
            w.titlebarAppearsTransparent = true
            w.titleVisibility = .hidden
            w.styleMask.insert(.fullSizeContentView)
            w.isMovableByWindowBackground = true
            w.contentView = NSHostingView(rootView: root)
            w.isReleasedWhenClosed = false
            w.center()
            settingsWindow = w
        }
        closeMenuPopover()
        settingsWindow?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        return true
    }

    @MainActor
    func showAbout() {
        if aboutWindow == nil {
            let host = NSHostingView(rootView: AboutView().environmentObject(settings))
            let w = NSWindow(contentRect: NSRect(origin: .zero, size: host.fittingSize),
                             styleMask: [.titled, .closable, .fullSizeContentView], backing: .buffered, defer: false)
            w.title = "About iXpat"
            w.titlebarAppearsTransparent = true
            w.titleVisibility = .hidden
            w.isMovableByWindowBackground = true
            w.contentView = host
            w.isReleasedWhenClosed = false
            w.center()
            aboutWindow = w
        }
        closeMenuPopover()
        aboutWindow?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    /// Opened from the menu popover: dismiss it, or it stays up next to the window.
    private func closeMenuPopover() {
        for w in NSApp.windows where w.isVisible && String(describing: type(of: w)).contains("MenuBarExtra") { w.close() }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showSettings(); return false
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        DebugLog.log("launch \(Bundle.main.bundlePath) version=\(AboutView.version) (\(AboutView.build)) arch=\(AboutView.arch) inputMonitoring=\(permissions.inputMonitoring) accessibility=\(permissions.accessibility)")
        Updater.shared.start { [weak self] in self?.settings.autoCheckUpdates ?? false }
        let force = ProcessInfo.processInfo.environment["IXPAT_OPEN"] != nil
        // Open the window on every launch (the status item can be hidden by a
        // full menu bar, so this is the one route the user can always count on).
        // IXPAT_NO_WINDOW=1 suppresses it for scripted launches.
        if force || ProcessInfo.processInfo.environment["IXPAT_NO_WINDOW"] == nil {
            showSettings()
        }
        engine.device = device
        engine.settings = settings
        // IXPAT_MENU_PREVIEW=1: host the menu popover in a plain window (design checks / screenshots).
        if ProcessInfo.processInfo.environment["IXPAT_MENU_PREVIEW"] != nil {
            let root = MenuView().environmentObject(settings).environmentObject(device).environmentObject(engine).environmentObject(permissions)
            let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 300, height: 360), styleMask: [.titled], backing: .buffered, defer: false)
            w.title = "MenuPreview"; w.contentView = NSHostingView(rootView: root); w.isReleasedWhenClosed = false; w.center(); w.orderFront(nil)
            menuPreview = w
        }
        if ProcessInfo.processInfo.environment["IXPAT_ABOUT"] != nil { showAbout() }   // screenshots

        // Mouse → haptic rules and the action button.
        device.onButtons = { [weak self] old, new in
            guard let self, self.settings.enabled else { return }
            let went = new & ~old, left = old & ~new
            if went & 0x01 != 0 { self.engine.play(.leftDown); self.armHold(0x01, .leftHold) }
            if left & 0x01 != 0 { self.engine.play(.leftUp); self.holdTimers[0x01]?.invalidate() }
            if went & 0x02 != 0 { self.engine.play(.rightDown); self.armHold(0x02, .rightHold) }
            if left & 0x02 != 0 { self.engine.play(.rightUp); self.holdTimers[0x02]?.invalidate() }
            if went & 0x04 != 0 { self.engine.play(.middleDown) }
        }
        device.onWheel = { [weak self] delta in
            guard let self else { return }
            let now = CACurrentMediaTime()
            let gap = now - self.lastNotchTime
            self.lastNotchTime = now
            self.notchTimes.append(now)
            self.notchTimes.removeAll { now - $0 > 0.25 }
            let rate = Double(self.notchTimes.count) / 0.25          // notches per second, recent window
            // Free-spin: a flick past the threshold silences the ratchet and
            // boosts distance until the wheel rests; the ratchet re-engages with a soft Align.
            if self.settings.enabled, self.settings.effFreeSpin {
                if !self.freeSpinActive, rate >= self.settings.freeSpinThreshold {
                    self.freeSpinActive = true
                    self.pushConfig()
                    self.engine.play(.freeSpinOn)
                }
                self.freeSpinTimer?.invalidate()
                self.freeSpinTimer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: false) { [weak self] _ in
                    guard let self, self.freeSpinActive else { return }
                    self.freeSpinActive = false
                    self.pushConfig()
                    self.engine.play(.freeSpinOff)
                }
            } else if self.freeSpinActive {
                self.freeSpinActive = false
                self.pushConfig()
            }
            if self.freeSpinActive {                        // free-spin: no ratchet, optional grain
                self.textureAccum += abs(delta)
                if self.textureAccum >= max(1, self.settings.effFreeSpinTextureEvery), now - self.lastRatchetTick >= 0.04 {
                    self.textureAccum = 0; self.lastRatchetTick = now
                    self.engine.play(.freeSpinTexture)
                }
                return
            }
            self.textureAccum = 0
            // Direction reversal while scrolling: one firmer Collide bump, like a detent catching.
            let dir = delta > 0 ? 1 : (delta < 0 ? -1 : 0)
            if self.settings.effRatchetDynamic, dir != 0, self.lastNotchDir != 0, dir != self.lastNotchDir, gap < 1.0 {
                self.lastNotchDir = dir
                self.wheelAccum = 0
                self.engine.play(.scrollStep, ordinal: self.ordinal(0x1012, fallback: 4), boost: 1)
                return
            }
            self.lastNotchDir = dir
            self.wheelAccum += abs(delta)
            let every = max(1, self.settings.effScrollStepEvery)
            guard self.wheelAccum >= every else { return }
            self.wheelAccum = 0
            guard self.settings.effRatchetDynamic else { self.engine.play(.scrollStep); return }
            if rate >= self.settings.effRatchetLightRate {
                // Fast: short light ticks, capped at 25/s so they stay distinct instead of smearing.
                guard now - self.lastRatchetTick >= 0.04 else { return }
                self.lastRatchetTick = now
                // Step, or Hover if the rule itself is Step (looked up in the device's own table).
                let step = self.ordinal(0x1014, fallback: 6), hover = self.ordinal(0x1008, fallback: 3)
                let light = self.settings.rule(.scrollStep).ordinal == step ? hover : step
                self.engine.play(.scrollStep, ordinal: light, boost: -1)
            } else {
                // Slow: a firm detent per notch; the first notch after a pause lands a touch harder.
                self.lastRatchetTick = now
                self.engine.play(.scrollStep, boost: gap > 0.6 ? 1 : 0)
            }
        }
        device.onReport = { [weak self] r in self?.button.handle(r) }
        device.onConnect = { [weak self] in
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { self?.applyPointer() }
        }

        // System events → haptic rules.
        // Tap callbacks arrive on the tap thread or the AX queue; everything that touches
        // app state hops to main. Settings flow the other way as lock-protected snapshots
        // (pushConfig), so the tap thread never reads a @Published value.
        monitor.onEvent = { [weak self] e in DispatchQueue.main.async { self?.engine.play(e) } }
        monitor.shouldSwallow = { [weak self] ev, t in self?.button.shouldSwallow(ev, type: t) ?? false }
        monitor.ringClick = { [weak self] in
            guard let self, self.ring.isOpen else { return false }
            DispatchQueue.main.async { self.ring.close(commit: true) }; return true
        }
        monitor.ringCancel = { [weak self] in
            guard let self, self.ring.isOpen else { return false }
            DispatchQueue.main.async { self.ring.close(commit: false) }; return true
        }
        monitor.start()

        // Action Ring
        ring.onDetent = { [weak self] in self?.engine.play(.ringDetent) }
        ring.onOpen = { [weak self] in self?.engine.play(.ringOpen) }
        ring.onCommit = { [weak self] item in
            self?.engine.play(.ringCommit)
            // Let the ring disappear before the action (shortcuts must reach the real target).
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.08) { self?.run(item.action, hold: false) }
        }
        button.onHoldEnd = { [weak self] in
            guard let self else { return }
            self.ring.holdEnded()
            let gestured = self.gestures.firedCount > 0
            self.gestures.end()
            if let a = self.pendingHoldAction {
                self.pendingHoldAction = nil
                if !gestured { self.run(a, hold: true) }      // a hold without movement is still the hold action
            }
        }
        // Hold-and-move gestures: the tap feeds pointer deltas while the button is held.
        monitor.gestureMove = { [weak self] dx, dy in self?.gestures.handleMove(dx: dx, dy: dy) ?? false }
        gestures.onAction = { [weak self] a in
            guard let self else { return }
            switch a {
            case .volumeUp, .volumeDown:
                // At the end of the range the system ignores the key; play a bump instead of a step.
                if let v = SystemVolume.current(), (a == .volumeUp && v >= 0.999) || (a == .volumeDown && v <= 0.001) {
                    self.engine.play(.gestureLimit); return
                }
                ActionRunner.run(a); self.engine.play(.gestureStep)
            case .brightnessUp, .brightnessDown:
                ActionRunner.run(a); self.engine.play(.gestureStep)
            case .previousTrack:
                self.run(a, hold: false); self.engine.play(.gestureSwipe, ordinal: self.ordinal(0x100A, fallback: 9))   // ERROR: the descending pattern
            default:
                self.run(a, hold: false); self.engine.play(.gestureSwipe)
            }
        }
        button.pressIntercept = { [weak self] in
            guard let self, self.ring.isOpen, !self.ring.holdMode else { return false }
            self.ring.close(commit: true); return true
        }
        // Menu highlight ticks (while the pointer is moving) and text-selection ticks (while dragging)
        menus.onHighlight = { [weak self] in self?.engine.play(.menuHighlight) }
        menus.pointerMovedRecently = { [weak self] in
            guard let self else { return false }
            return CACurrentMediaTime() - self.monitor.lastPointerMove < 0.5
        }
        menus.onSelectionGrow = { [weak self] step in self?.engine.play(step == .line ? .textLine : .textSelect) }
        menus.selectionGate = { [weak self] in
            guard let self else { return false }
            return self.monitor.leftButtonDown && !self.ring.isOpen
        }
        menus.onControl = { [weak self] in self?.engine.play(.controlHover) }
        menus.onDropTarget = { [weak self] in self?.engine.play(.dropTarget) }
        menus.leftDown = { [weak self] in self?.monitor.leftButtonDown ?? false }
        menus.start()

        // Cursor-shape hover ticks (main thread, cheap) and the drag badge.
        hover.pointerMovedRecently = { [weak self] in
            guard let self else { return false }
            return CACurrentMediaTime() - self.monitor.lastPointerMove < 0.3
        }
        hover.leftButtonDown = { [weak self] in self?.monitor.leftButtonDown ?? false }
        hover.onHover = { [weak self] in self?.engine.play(.hoverAffordance) }
        hover.onDropTarget = { [weak self] in self?.engine.play(.dropTarget) }
        hover.start()

        // Accessibility notifications from the front app: sliders, selections, scroll ends.
        axWatch.pointerActiveRecently = { [weak self] in
            guard let self else { return false }
            let now = CACurrentMediaTime()
            return now - self.monitor.lastPointerMove < 0.5 || now - self.monitor.lastClick < 0.5
        }
        axWatch.onSlider = { [weak self] in self?.engine.play(.sliderTick) }
        axWatch.onSelection = { [weak self] in self?.engine.play(.selectionChange) }
        axWatch.onScrollEnd = { [weak self] in self?.engine.play(.scrollEnd) }
        if let front = NSWorkspace.shared.frontmostApplication { axWatch.observe(pid: front.processIdentifier); settings.frontBundleID = front.bundleIdentifier }

        // Plugin bridge: apps ask for a pulse by waveform name.
        bridge.onPlay = { [weak self] name, intensity in
            guard let self, self.settings.enabled, self.settings.effHapticsEnabled, self.settings.rule(.pluginPulse).enabled,
                  self.settings.masterIntensity > 0,                       // master "Off" silences plugins too
                  let wf = self.device.waveforms.first(where: { $0.name.lowercased() == name }) else { return }
            let scaled = max(1, Int((Double(intensity) * Double(self.settings.masterIntensity) / 4.0).rounded()))
            self.engine.play(ordinal: wf.ordinal, intensity: scaled, label: "plugin " + name)
        }
        bridge.waveformsJSON = { [weak self] in
            let list = (self?.device.waveforms ?? []).map { "{\"name\":\"\($0.name.lowercased())\",\"ordinal\":\($0.ordinal),\"ms\":\($0.durationMs)}" }
            return "[" + list.joined(separator: ",") + "]"
        }

        // Settings → the other threads, as snapshots.
        pushConfig()
        settings.objectWillChange.receive(on: DispatchQueue.main).sink { [weak self] _ in self?.pushConfig() }.store(in: &bag)
        device.$connected.receive(on: DispatchQueue.main).sink { [weak self] on in
            guard let self else { return }
            if !on {                                   // a hold cannot end normally once the mouse is gone
                self.gestures.end(); self.pendingHoldAction = nil; self.ring.holdEnded()
            }
            self.pushConfig()
        }.store(in: &bag)
        DebugLog.log("event tap running=\(monitor.running) listenOnly=\(monitor.listenOnly)")

        button.onGesture = { [weak self] g in
            guard let self, self.settings.enabled else { return }
            self.engine.play(.actionButton)
            guard let a = self.settings.actions[g] else { return }
            // A hold starts gesture mode (unless it drives the ring, which needs the pointer); its own
            // action waits for the release and runs only when the mouse stayed put.
            if g == .hold, self.settings.gesturesOnHold, a.kind != .actionRing, a.kind != .gestures, self.button.pressed {
                self.pendingHoldAction = a
                self.gestures.begin()
                return
            }
            self.run(a, hold: g == .hold)
        }

        NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.didActivateApplicationNotification,
                                                          object: nil, queue: .main) { [weak self] n in
            guard let self else { return }
            guard let app = n.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication else { return }
            self.settings.frontBundleID = app.bundleIdentifier
            if app.processIdentifier == ProcessInfo.processInfo.processIdentifier { return }
            self.axWatch.observe(pid: app.processIdentifier)
            self.engine.play(.appSwitch)
        }
        NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.didWakeNotification,
                                                          object: nil, queue: .main) { [weak self] _ in
            self?.monitor.restart()
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { self?.applyPointer() }
        }

        // Ask for Input Monitoring once, after launch has settled: a request made
        // while the app is still registering with the system is dropped silently
        // and leaves no entry in the Settings list.
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
            guard let self, !self.permissions.inputMonitoring,
                  !UserDefaults.standard.bool(forKey: "inputMonitoringRequested") else { return }
            UserDefaults.standard.set(true, forKey: "inputMonitoringRequested")
            let r = IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)
            DebugLog.log("IOHIDRequestAccess(ListenEvent) after launch returned \(r)")
        }

        permissions.onGranted = { [weak self] kind in
            guard let self else { return }
            switch kind {
            case .inputMonitoring: self.device.enumerate()
            case .accessibility:   self.monitor.restart()
            }
        }

        // Pointer settings: re-apply whenever the relevant values change.
        settings.$managePointer.dropFirst().sink { [weak self] on in
            // @Published emits before the property is stored: apply after the store so applyPointer sees the new value.
            DispatchQueue.main.async { on ? self?.applyPointer() : PointerSettings.restoreSystem() }
        }.store(in: &bag)
        Publishers.CombineLatest3(settings.$pointerSpeed, settings.$noAcceleration, settings.$scrollSpeed)
            .dropFirst()
            .debounce(for: .milliseconds(250), scheduler: DispatchQueue.main)
            .sink { [weak self] _ in self?.applyPointer() }
            .store(in: &bag)
    }

    /// Run an action; the ring needs app state, everything else is self-contained.
    func run(_ a: ButtonAction, hold: Bool) {
        DebugLog.log("run action \(a.kind.title) hold=\(hold) ringOpen=\(ring.isOpen)")   // kind only: a shell command may hold private text
        if case .actionRing = a {
            if hold { ring.open(items: settings.effRingItems, holdMode: true) }
            else { ring.toggle(items: settings.effRingItems, holdMode: false) }
        } else if case .gestures = a {
            // Only while the chord is really down: the test button must not freeze the pointer.
            if hold, button.pressed { gestures.begin() }
        } else {
            ActionRunner.run(a)
        }
    }

    /// Start the press-and-hold timer for a mouse button (main thread).
    private func armHold(_ mask: UInt8, _ event: HapticEvent) {
        holdTimers[mask]?.invalidate()
        guard settings.rule(event).enabled else { return }
        holdTimers[mask] = Timer.scheduledTimer(withTimeInterval: Double(settings.clickHoldMs) / 1000, repeats: false) { [weak self] _ in
            guard let self, self.device.buttons & mask != 0 else { return }
            self.engine.play(event)
        }
    }

    private func applyPointer() {
        guard settings.managePointer, device.connected else { return }
        PointerSettings.apply(speed: settings.pointerSpeed, noAcceleration: settings.noAcceleration, scrollSpeed: settings.scrollSpeed)
    }

    /// Snapshot the settings the tap thread and the AX queue need. Runs on main.
    private func pushConfig() {
        let s = settings
        // Scrolling is shaped only for this mouse: while it is attached, or shortly after a
        // Bluetooth idle sleep, so the first notches that wake it are not left unshaped.
        let recent = device.lastDetach.map { Date().timeIntervalSince($0) < 1800 } ?? false
        let live = s.enabled && (device.connected || recent)
        monitor.smooth = SmoothConfig(enabled: live && s.effSmoothScroll,
                                      pxPerNotch: s.scrollPixelsPerNotch,
                                      glide: s.scrollGlide,
                                      momentum: s.scrollMomentum,
                                      accel: s.scrollAccel,
                                      multiplier: freeSpinActive ? s.freeSpinMultiplier : 1,
                                      reverse: live && s.reverseScroll)
        let touch = ForceClick.touchDeviceIDs()
        monitor.force = ForceConfig(enabled: s.enabled && s.forceClick && permissions.accessibility,   // any plain mouse; trackpads are skipped by device ID
                                    delay: Double(s.forceClickMs) / 1000,
                                    deviceID: touch.first ?? 0,
                                    touchDevices: Set(touch))
        menus.config = MenuWatcher.Config(menuTicks: s.enabled && s.rule(.menuHighlight).enabled,
                                          textTicks: s.enabled && (s.rule(.textSelect).enabled || s.rule(.textLine).enabled),
                                          controlTicks: s.enabled && s.rule(.controlHover).enabled,
                                          dropTicks: s.enabled && s.rule(.dropTarget).enabled)
        hover.config = HoverWatcher.Config(hoverTicks: s.enabled && s.rule(.hoverAffordance).enabled,
                                           dropTicks: s.enabled && s.rule(.dropTarget).enabled)
        axWatch.config = AXWatcher.Config(sliderTicks: s.enabled && s.rule(.sliderTick).enabled,
                                          selectionTicks: s.enabled && s.rule(.selectionChange).enabled,
                                          scrollEndTicks: s.enabled && s.rule(.scrollEnd).enabled)
        if s.bridgeEnabled, !bridge.running { bridge.start(port: UInt16(clamping: s.bridgePort), token: s.bridgeToken) }
        else if !s.bridgeEnabled, bridge.running { bridge.stop() }
        button.swallowEnabled = s.enabled && s.swallowButton
        if !s.enabled { gestures.end() }
        ring.accentHex = s.accentHex
        gestures.config = GestureController.Config(up: s.effGestureUp, down: s.effGestureDown, left: s.effGestureLeft, right: s.effGestureRight,
                                                   stepPx: s.gestureStepPx, swipePx: s.gestureSwipePx, freezePointer: s.gestureFreezePointer)
    }

    /// Ordinal of a haptic waveform usage in the connected device's table.
    private func ordinal(_ usage: UInt16, fallback: Int) -> Int {
        Waveform.ordinal(for: usage, in: device.waveforms) ?? fallback
    }

    func applicationWillTerminate(_ notification: Notification) {
        settings.flush()
        if settings.managePointer { PointerSettings.restoreSystem() }
        gestures.end()                                 // never leave the pointer detached from the cursor
        monitor.stop()
        bridge.stop()
        hover.stop()
        axWatch.detach()
    }
}

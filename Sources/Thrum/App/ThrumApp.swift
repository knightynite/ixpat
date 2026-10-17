import SwiftUI
import AppKit
import Combine
import QuartzCore
import IOKit.hid

@main
struct ThrumApp: App {
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
    let settings = Settings()
    let device = MouseDevice()
    let engine = HapticEngine()
    let permissions = Permissions()
    let monitor = EventMonitor()
    let menus = MenuWatcher()
    let ring = RadialMenuController.shared
    let button = ActionButtonController()

    // Wheel feel: notch rate → free-spin (MX SmartShift style)
    private var notchTimes: [CFTimeInterval] = []
    private var freeSpinActive = false
    private var freeSpinTimer: Timer?

    private var settingsWindow: NSWindow?
    private var bag = Set<AnyCancellable>()
    private var wheelAccum = 0
    private var lastNotchTime: CFTimeInterval = 0
    private var lastNotchDir = 0
    private var lastRatchetTick: CFTimeInterval = 0

    /// The settings window is a plain NSWindow hosting the SwiftUI view, so it
    /// can be opened from anywhere (first launch, Finder reopen, the menu) even
    /// when the status item is hidden by a full menu bar or a notch.
    @MainActor @discardableResult
    func showSettings() -> Bool {
        if settingsWindow == nil {
            let root = MainWindow()
                .environmentObject(settings).environmentObject(device).environmentObject(engine)
                .environmentObject(permissions).environmentObject(button)
            let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 860, height: 700),
                             styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
            w.title = "Thrum"
            w.contentView = NSHostingView(rootView: root)
            w.isReleasedWhenClosed = false
            w.center()
            settingsWindow = w
        }
        settingsWindow?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        return true
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showSettings(); return false
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        DebugLog.log("launch \(Bundle.main.bundlePath) inputMonitoring=\(permissions.inputMonitoring) accessibility=\(permissions.accessibility)")
        let force = ProcessInfo.processInfo.environment["THRUM_OPEN"] != nil
        // Open the window on every launch (the status item can be hidden by a
        // full menu bar, so this is the one route the user can always count on).
        // THRUM_NO_WINDOW=1 suppresses it for scripted launches.
        if force || ProcessInfo.processInfo.environment["THRUM_NO_WINDOW"] == nil {
            showSettings()
        }
        engine.device = device
        engine.settings = settings

        // Mouse → haptic rules and the action button.
        device.onButtons = { [weak self] old, new in
            guard let self, self.settings.enabled else { return }
            let went = new & ~old, left = old & ~new
            if went & 0x01 != 0 { self.engine.play(.leftDown) }
            if left & 0x01 != 0 { self.engine.play(.leftUp) }
            if went & 0x02 != 0 { self.engine.play(.rightDown) }
            if left & 0x02 != 0 { self.engine.play(.rightUp) }
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
            // Free-spin (SmartShift): a flick past the threshold silences the ratchet and
            // boosts distance until the wheel rests; the ratchet re-engages with a soft Align.
            if self.settings.enabled, self.settings.freeSpin {
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
            guard !self.freeSpinActive else { return }      // free-spin: no ratchet ticks
            // Direction reversal while scrolling: one firmer Collide bump, like a detent catching.
            let dir = delta > 0 ? 1 : (delta < 0 ? -1 : 0)
            if self.settings.ratchetDynamic, dir != 0, self.lastNotchDir != 0, dir != self.lastNotchDir, gap < 1.0 {
                self.lastNotchDir = dir
                self.wheelAccum = 0
                self.engine.play(.scrollStep, ordinal: self.ordinal(0x1012, fallback: 4), boost: 1)
                return
            }
            self.lastNotchDir = dir
            self.wheelAccum += abs(delta)
            let every = max(1, self.settings.scrollStepEvery)
            guard self.wheelAccum >= every else { return }
            self.wheelAccum = 0
            guard self.settings.ratchetDynamic else { self.engine.play(.scrollStep); return }
            if rate >= self.settings.ratchetLightRate {
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
        button.onHoldEnd = { [weak self] in self?.ring.holdEnded() }
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
        menus.start()

        // Settings → the other threads, as snapshots.
        pushConfig()
        settings.objectWillChange.receive(on: DispatchQueue.main).sink { [weak self] _ in self?.pushConfig() }.store(in: &bag)
        device.$connected.receive(on: DispatchQueue.main).sink { [weak self] _ in self?.pushConfig() }.store(in: &bag)
        DebugLog.log("event tap running=\(monitor.running) listenOnly=\(monitor.listenOnly)")

        button.onGesture = { [weak self] g in
            guard let self, self.settings.enabled else { return }
            self.engine.play(.actionButton)
            if let a = self.settings.actions[g] { self.run(a, hold: g == .hold) }
        }

        NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.didActivateApplicationNotification,
                                                          object: nil, queue: .main) { [weak self] n in
            guard let self else { return }
            if let app = n.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication,
               app.processIdentifier == ProcessInfo.processInfo.processIdentifier { return }
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
            on ? self?.applyPointer() : PointerSettings.restoreSystem()
        }.store(in: &bag)
        Publishers.CombineLatest3(settings.$pointerSpeed, settings.$noAcceleration, settings.$scrollSpeed)
            .dropFirst()
            .debounce(for: .milliseconds(250), scheduler: DispatchQueue.main)
            .sink { [weak self] _ in self?.applyPointer() }
            .store(in: &bag)
    }

    /// Run an action; the ring needs app state, everything else is self-contained.
    func run(_ a: ButtonAction, hold: Bool) {
        DebugLog.log("run action \(a.describe) hold=\(hold) ringOpen=\(ring.isOpen)")
        if case .actionRing = a {
            if hold { ring.open(items: settings.ringItems, holdMode: true) }
            else { ring.toggle(items: settings.ringItems, holdMode: false) }
        } else {
            ActionRunner.run(a)
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
        monitor.smooth = SmoothConfig(enabled: live && s.smoothScroll,
                                      pxPerNotch: s.scrollPixelsPerNotch,
                                      glide: s.scrollGlide,
                                      momentum: s.scrollMomentum,
                                      accel: s.scrollAccel,
                                      multiplier: freeSpinActive ? s.freeSpinMultiplier : 1,
                                      reverse: live && s.reverseScroll)
        menus.config = MenuWatcher.Config(menuTicks: s.enabled && s.rule(.menuHighlight).enabled,
                                          textTicks: s.enabled && (s.rule(.textSelect).enabled || s.rule(.textLine).enabled))
        button.swallowEnabled = s.enabled && s.swallowButton
        ring.accentHex = s.accentHex
    }

    /// Ordinal of a haptic waveform usage in the connected device's table.
    private func ordinal(_ usage: UInt16, fallback: Int) -> Int {
        Waveform.ordinal(for: usage, in: device.waveforms) ?? fallback
    }

    func applicationWillTerminate(_ notification: Notification) {
        settings.flush()
        if settings.managePointer { PointerSettings.restoreSystem() }
        monitor.stop()
    }
}

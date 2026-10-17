import SwiftUI
import AppKit

enum TabKind: String, CaseIterable, Identifiable {
    case haptics, button, ring, apps, pointer, diagnostics
    var id: String { rawValue }
    var title: String {
        switch self {
        case .haptics: return "Haptics"
        case .button: return "Action Button"
        case .ring: return "Action Ring"
        case .apps: return "Apps"
        case .pointer: return "Pointer & Scroll"
        case .diagnostics: return "Diagnostics"
        }
    }
}

struct MainWindow: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var engine: HapticEngine
    @EnvironmentObject var permissions: Permissions
    @EnvironmentObject var button: ActionButtonController
    @Environment(\.colorScheme) private var systemScheme
    @State private var tab: TabKind = .haptics

    private var palette: Palette { Palette.resolve(theme: settings.theme, systemScheme: systemScheme, accent: settings.accent) }

    var body: some View {
        let pal = palette
        ZStack {
            Backdrop(pal: pal)
            VStack(spacing: 0) {
                HeaderBar()
                ScrollView {
                    VStack(spacing: 16) {
                        HStack(spacing: 16) {
                            heroTile.frame(width: 330, height: 300)
                            LiveTile().frame(height: 300)
                        }
                        HStack {
                            PillTabs(selection: $tab, title: { $0.title }, pal: pal)
                            Spacer()
                        }
                        Group {
                            switch tab {
                            case .haptics: HapticsPane()
                            case .button: ButtonPane()
                            case .ring: RingPane()
                            case .apps: AppsPane()
                            case .pointer: PointerPane()
                            case .diagnostics: DiagnosticsPane()
                            }
                        }
                        .padding(18)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .glass(pal)
                    }
                    .padding(.horizontal, 20).padding(.top, 8).padding(.bottom, 22)
                }
            }
        }
        .environment(\.palette, pal)
        .preferredColorScheme(settings.theme == .system ? nil : (pal.isDark ? .dark : .light))
        .tint(pal.accent)
        .frame(width: 860, height: 700)
    }

    private var heroTile: some View {
        let pal = palette
        return ZStack {
            VStack(alignment: .leading) {
                HStack {
                    Text("SURFACE MOUSE · 2ND EDITION").font(.system(size: 10, weight: .semibold)).foregroundStyle(pal.text2).tracking(0.8)
                    Spacer()
                    HStack(spacing: 5) {
                        Circle().fill(device.connected ? Color.green : pal.text3).frame(width: 7, height: 7)
                            .shadow(color: device.connected ? .green.opacity(0.8) : .clear, radius: 4)
                        Text(device.connected ? "Connected" : "Offline").font(.system(size: 10, weight: .medium)).foregroundStyle(pal.text2)
                    }
                    .padding(.horizontal, 8).padding(.vertical, 3)
                    .background(Capsule().fill(pal.field))
                }
                Spacer()
                HStack {
                    BatteryBadge(percent: device.battery)
                    Spacer()
                    Text("fw \(device.firmware)").font(.system(size: 10, design: .monospaced)).foregroundStyle(pal.text3)
                }
            }.padding(14)
            MouseSchematic().padding(.vertical, 30)
                .shadow(color: .black.opacity(pal.isDark ? 0.5 : 0.18), radius: 22, y: 14)
        }
        .glass(pal, tint: pal.accent)
    }
}

struct HeaderBar: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @Environment(\.palette) private var pal
    var body: some View {
        HStack(spacing: 12) {
            Image(nsImage: NSApp.applicationIconImage).resizable().interpolation(.high).frame(width: 34, height: 34)
                .shadow(color: pal.accent.opacity(0.45), radius: 8, y: 3)
            VStack(alignment: .leading, spacing: 1) {
                Wordmark(pal: pal, size: 18)
                Text("Haptics, gestures and feel for the Surface Mouse on macOS").font(.system(size: 11)).foregroundStyle(pal.text2)
            }
            Spacer()
            Toggle("", isOn: $settings.enabled).toggleStyle(.switch).labelsHidden()
        }
        .padding(.leading, 86).padding(.trailing, 20).padding(.top, 14).padding(.bottom, 12)
    }
}

/// Right-hand hero tile: what just happened.
struct LiveTile: View {
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var engine: HapticEngine
    @EnvironmentObject var settings: Settings
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("LIVE").font(.system(size: 10, weight: .semibold)).foregroundStyle(pal.text2).tracking(0.8)
                Spacer()
                Text("\(engine.pulseCount) pulses").font(.system(size: 10, design: .monospaced)).foregroundStyle(pal.text3)
            }
            VStack(alignment: .leading, spacing: 12) {
                stat("Last event", engine.lastEvent)
                stat("Last pulse", engine.lastPlayed)
            }
            Spacer(minLength: 0)
        }
        .padding(14)
        .glass(pal)
    }

    private func stat(_ k: String, _ v: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(k).font(.system(size: 10, weight: .semibold)).foregroundStyle(pal.text3)
            Text(v).font(.system(size: 12, weight: .medium)).foregroundStyle(pal.text).lineLimit(1)
        }
    }
}

// MARK: - Haptics pane

struct HapticsPane: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var engine: HapticEngine
    @EnvironmentObject var permissions: Permissions
    @Environment(\.palette) private var pal
    private let levels = ["Off", "Light", "Medium", "Strong", "Max"]

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Toggle(isOn: $settings.hapticsEnabled) { Text("Haptic feedback").font(.system(size: 13, weight: .semibold)) }.toggleStyle(.switch)
                Spacer()
                Text("Strength").font(.system(size: 12)).foregroundStyle(pal.text2)
                Picker("", selection: $settings.masterIntensity) {
                    ForEach(0..<5, id: \.self) { Text(levels[$0]).tag($0) }
                }.pickerStyle(.segmented).labelsHidden().frame(width: 300)
            }
            Text("Each event below plays one of the waveforms the mouse itself reports (HID Simple Haptic Controller). "
               + "The mouse's own click feel is in its firmware; these are extra pulses layered on top.")
                .font(.system(size: 11)).foregroundStyle(pal.text2)
            Divider()
            FeelPresets()
            Divider()
            ForEach(HapticEvent.allCases) { ev in
                RuleRow(event: ev)
                if ev != HapticEvent.allCases.last { Divider().opacity(0.5) }
            }
        }
    }
}

struct RuleRow: View {
    let event: HapticEvent
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var engine: HapticEngine
    @EnvironmentObject var permissions: Permissions
    @Environment(\.palette) private var pal

    var body: some View {
        let rule = settings.binding(event)
        HStack(spacing: 12) {
            Toggle("", isOn: rule.enabled).toggleStyle(.switch).labelsHidden()
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text(event.title).font(.system(size: 13, weight: .medium)).foregroundStyle(pal.text)
                    if let p = event.permission, !permissions.granted(p) {
                        Text("needs \(p.title)").font(.system(size: 10, weight: .semibold)).foregroundStyle(.orange)
                    }
                }
                Text(event.detail).font(.system(size: 11)).foregroundStyle(pal.text2)
            }
            Spacer()
            if event == .scrollStep {
                Stepper("every \(settings.scrollStepEvery)", value: $settings.scrollStepEvery, in: 1...10)
                    .font(.system(size: 11)).frame(width: 110)
            }
            if event == .freeSpinTexture {
                Stepper("every \(settings.freeSpinTextureEvery)", value: $settings.freeSpinTextureEvery, in: 1...6)
                    .font(.system(size: 11)).frame(width: 110)
            }
            if event == .leftHold {
                Stepper("after \(settings.clickHoldMs) ms", value: $settings.clickHoldMs, in: 150...1500, step: 50)
                    .font(.system(size: 11)).frame(width: 130)
            }
            if let e = rule.wrappedValue.echo {
                Text("+ \(device.waveforms.first { $0.ordinal == e.ordinal }?.name ?? "#\(e.ordinal)") \(e.intensity) @\(e.delayMs) ms")
                    .font(.system(size: 10, design: .monospaced)).foregroundStyle(pal.text2)
                Button { rule.wrappedValue.echo = nil } label: { Image(systemName: "xmark.circle") }.buttonStyle(.plain).help("Remove the second pulse")
            }
            Picker("", selection: rule.ordinal) {
                ForEach(device.waveforms) { Text($0.name).tag($0.ordinal) }
            }.labelsHidden().frame(width: 110)
            Picker("", selection: rule.intensity) {
                ForEach(1..<5, id: \.self) { Text("\($0)").tag($0) }
            }.pickerStyle(.segmented).labelsHidden().frame(width: 110)
            Button {
                engine.preview(rule.wrappedValue, label: "Test")
            } label: { Image(systemName: "play.fill") }
            .buttonStyle(.bordered).disabled(!device.connected)
        }
        .padding(.vertical, 4)
        .opacity(rule.wrappedValue.enabled ? 1 : 0.6)
    }
}

// MARK: - Action button pane

struct ButtonPane: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var button: ActionButtonController
    @EnvironmentObject var permissions: Permissions
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            VStack(alignment: .leading, spacing: 3) {
                Text("How the mouse reports its action button").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
                Text("The firmware decides the gesture and sends a Windows chord for each: press = Win+Shift+S (Snipping Tool), "
                   + "double press = Win + a fake left click (Click to Do), hold = Win+Shift+F23, the Copilot key, held. iXpat maps those chords "
                   + "straight to the actions below and hides them from your apps.")
                    .font(.system(size: 11)).foregroundStyle(pal.text2)
                HStack(spacing: 8) {
                    Text("Last gesture:").font(.system(size: 11)).foregroundStyle(pal.text2)
                    Text(button.lastGesture).font(.system(size: 11, weight: .semibold)).foregroundStyle(pal.accent)
                }
            }
            if !permissions.inputMonitoring {
                Text("Reading the button needs Input Monitoring (Diagnostics tab).").font(.system(size: 11)).foregroundStyle(.orange)
            }
            Divider()
            ForEach(ButtonGesture.allCases) { g in
                ActionRow(gesture: g)
            }
            Divider()
            Toggle(isOn: $settings.gesturesOnHold) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Hold-and-move gestures on every hold").font(.system(size: 13, weight: .medium))
                    Text("Keep the button down and move to gesture; release without moving and the hold action above runs as usual. Off: only a hold mapped to “Hold-and-move gestures” gestures.")
                        .font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch)
            if settings.gesturesOnHold || settings.actions.values.contains(.gestures) {
                GesturePane()
            }
            Divider()
            Toggle(isOn: $settings.swallowButton) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Hide the button's built-in chords from apps").font(.system(size: 13, weight: .medium))
                    Text("Otherwise a press also reaches the frontmost app as ⌘⇧S and the double press as a real click. Needs Accessibility.")
                        .font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch).disabled(!permissions.accessibility)
        }
    }
}

/// Picker for a ButtonAction plus the detail control its kind needs.
struct ActionPicker: View {
    @Binding var action: ButtonAction
    @Environment(\.palette) private var pal
    @State private var recording = false
    @State private var monitor: Any?

    var body: some View {
        HStack(spacing: 10) {
            Picker("", selection: Binding(get: { action.kind }, set: { action = ButtonAction.blank($0) })) {
                ForEach(ButtonAction.Kind.allCases) { Text($0.title).tag($0) }
            }.labelsHidden().frame(width: 180)
            switch action {
            case .launchApp(let path):
                Button(path.isEmpty ? "Choose app…" : ((path as NSString).lastPathComponent as NSString).deletingPathExtension) { chooseApp() }
            case .shortcut(_, _, let label):
                Button(recording ? "Press keys…" : (label.isEmpty ? "Record shortcut" : label)) { record() }.buttonStyle(.bordered)
            case .shell(let cmd):
                TextField("zsh command", text: Binding(get: { cmd }, set: { action = .shell(command: $0) }))
                    .textFieldStyle(.roundedBorder).frame(width: 220)
            case .actionRing:
                Text("opens the ring (hold: sweep and release · press: click a chamber)").font(.system(size: 11)).foregroundStyle(pal.text2)
            case .gestures:
                Text("hold, then move the mouse: ↑ ↓ ← → run the gesture actions below").font(.system(size: 11)).foregroundStyle(pal.text2)
            default:
                EmptyView()
            }
        }
        .onDisappear { stopRecording() }
    }

    private func chooseApp() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.applicationBundle]
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url { action = .launchApp(path: url.path) }
    }

    private func record() {
        guard !recording else { return }
        recording = true
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { ev in
            if ev.keyCode != 53 {                                   // Escape cancels
                let flags = CGEventFlags(rawValue: UInt64(ev.modifierFlags.intersection([.command, .shift, .control, .option]).rawValue))
                let label = ActionRunner.shortcutLabel(keyCode: Int(ev.keyCode), flags: flags)
                action = .shortcut(keyCode: Int(ev.keyCode), flags: flags.rawValue, label: label)
            }
            stopRecording()
            return nil
        }
    }

    private func stopRecording() {
        if let m = monitor { NSEvent.removeMonitor(m) }
        monitor = nil; recording = false
    }
}

struct ActionRow: View {
    let gesture: ButtonGesture
    @EnvironmentObject var settings: Settings
    @Environment(\.palette) private var pal

    var body: some View {
        HStack(spacing: 12) {
            Text(gesture.title).font(.system(size: 13, weight: .medium)).foregroundStyle(pal.text).frame(width: 120, alignment: .leading)
            ActionPicker(action: Binding(get: { settings.actions[gesture] ?? .none }, set: { settings.actions[gesture] = $0 }))
            Spacer()
            Button { AppDelegate.shared?.run(settings.actions[gesture] ?? .none, hold: gesture == .hold) } label: { Image(systemName: "play.fill") }
                .buttonStyle(.bordered).help("Run this action now")
        }
        .padding(.vertical, 4)
    }
}

/// Hold-and-move gesture settings (shown when a gesture is mapped to "Hold-and-move gestures").
struct GesturePane: View {
    @EnvironmentObject var settings: Settings
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            VStack(alignment: .leading, spacing: 3) {
                Text("Hold-and-move gestures").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
                Text("Keep the action button down and move the mouse. The pointer stays put; the first few pixels pick the axis. "
                   + "Up / down actions marked (step) repeat as you keep moving, with a tick each; sideways swipes fire once per swipe.")
                    .font(.system(size: 11)).foregroundStyle(pal.text2)
            }
            row("Move up", $settings.gestureUp)
            row("Move down", $settings.gestureDown)
            row("Move left", $settings.gestureLeft)
            row("Move right", $settings.gestureRight)
            HStack {
                Text("Step size").font(.system(size: 12)).frame(width: 120, alignment: .leading)
                Slider(value: $settings.gestureStepPx, in: 8...80, step: 2) { } minimumValueLabel: { Text("Fine").font(.system(size: 10)) } maximumValueLabel: { Text("Coarse").font(.system(size: 10)) }
                Text("\(Int(settings.gestureStepPx)) px").font(.system(size: 11, design: .monospaced)).frame(width: 50)
            }
            HStack {
                Text("Swipe length").font(.system(size: 12)).frame(width: 120, alignment: .leading)
                Slider(value: $settings.gestureSwipePx, in: 30...250, step: 10) { } minimumValueLabel: { Text("Short").font(.system(size: 10)) } maximumValueLabel: { Text("Long").font(.system(size: 10)) }
                Text("\(Int(settings.gestureSwipePx)) px").font(.system(size: 11, design: .monospaced)).frame(width: 50)
            }
            Toggle(isOn: $settings.gestureFreezePointer) {
                Text("Freeze the pointer while gesturing").font(.system(size: 12))
            }.toggleStyle(.switch)
        }
    }

    private func row(_ label: String, _ action: Binding<ButtonAction>) -> some View {
        HStack(spacing: 12) {
            Text(label).font(.system(size: 13, weight: .medium)).foregroundStyle(pal.text).frame(width: 120, alignment: .leading)
            ActionPicker(action: action)
            Spacer()
        }
    }
}

/// Named feels for the two buttons and the wheel; each writes several rules at once.
struct FeelPresets: View {
    @EnvironmentObject var settings: Settings
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Feel presets").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
            Text("Pick a feel for each button and the wheel; the rows below are what it sets, and stay editable (the picker reads Custom once you change them).")
                .font(.system(size: 11)).foregroundStyle(pal.text2)
            HStack(spacing: 10) {
                clickPicker("Left click", right: false)
                clickPicker("Right click", right: true)
                wheelPicker()
            }
            Text(blurb).font(.system(size: 11)).foregroundStyle(pal.text2)
        }
    }

    private var blurb: String {
        let l = ClickFeel.all.first { $0.name == settings.clickFeelName(right: false) }?.blurb ?? "custom rules."
        let w = WheelFeel.all.first { $0.name == settings.wheelFeelName() }?.blurb ?? "custom rules."
        return "Left click: " + l + "   Wheel: " + w
    }

    private func clickPicker(_ label: String, right: Bool) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.system(size: 11)).foregroundStyle(pal.text2)
            Picker("", selection: Binding(get: { settings.clickFeelName(right: right) },
                                          set: { n in if let f = ClickFeel.all.first(where: { $0.name == n }) { settings.applyClickFeel(f, right: right) } })) {
                ForEach(ClickFeel.all) { Text($0.name).tag($0.name) }
                if settings.clickFeelName(right: right) == "Custom" { Text("Custom").tag("Custom") }
            }.labelsHidden().frame(width: 150)
        }
    }

    private func wheelPicker() -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("Scroll wheel").font(.system(size: 11)).foregroundStyle(pal.text2)
            Picker("", selection: Binding(get: { settings.wheelFeelName() },
                                          set: { n in if let f = WheelFeel.all.first(where: { $0.name == n }) { settings.applyWheelFeel(f) } })) {
                ForEach(WheelFeel.all) { Text($0.name).tag($0.name) }
                if settings.wheelFeelName() == "Custom" { Text("Custom").tag("Custom") }
            }.labelsHidden().frame(width: 150)
        }
    }
}

// MARK: - Action Ring pane

struct RingPane: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var permissions: Permissions
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                VStack(alignment: .leading, spacing: 3) {
                    Text("Action Ring").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
                    Text("A ring of shortcuts that opens under the pointer. Assign it to the action button (press or hold). "
                       + "Sweep the mouse toward a chamber (you feel a detent per chamber), then release or click to run it. Escape or right-click cancels.")
                        .font(.system(size: 11)).foregroundStyle(pal.text2)
                }
                Spacer()
                Button {
                    AppDelegate.shared?.run(.actionRing, hold: false)
                } label: { Label("Open ring now", systemImage: "circle.hexagongrid") }
                .buttonStyle(.borderedProminent).disabled(!permissions.accessibility)
            }
            Divider()
            RingEditor(items: $settings.ringItems)
        }
    }
}

/// The chamber rows, shared by the default ring and per-app rings.
struct RingEditor: View {
    @Binding var items: [RingItem]
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(Array(items.enumerated()), id: \.element.id) { i, item in
                HStack(spacing: 10) {
                    Image(systemName: item.symbol.isEmpty ? "questionmark" : item.symbol)
                        .font(.system(size: 16)).frame(width: 34, height: 34)
                        .background(Circle().fill(pal.accent.opacity(0.18)))
                    TextField("Title", text: Binding(get: { item.title }, set: { v in if items.indices.contains(i) { items[i].title = v } }))
                        .textFieldStyle(.roundedBorder).frame(width: 130)
                    TextField("SF Symbol", text: Binding(get: { item.symbol }, set: { v in if items.indices.contains(i) { items[i].symbol = v } }))
                        .textFieldStyle(.roundedBorder).frame(width: 150).font(.system(size: 11, design: .monospaced))
                    ActionPicker(action: Binding(get: { item.action }, set: { v in if items.indices.contains(i) { items[i].action = v } }))
                    Spacer()
                    Button { if items.indices.contains(i) { items.remove(at: i) } } label: { Image(systemName: "minus.circle") }
                        .buttonStyle(.plain).foregroundStyle(pal.text2).disabled(items.count <= 2)
                }
            }
            HStack {
                Button { items.append(RingItem(title: "New", symbol: "star", action: .none)) } label: { Label("Add chamber", systemImage: "plus") }
                    .disabled(items.count >= 8)
                Button("Reset to defaults") { items = RingItem.defaults }
                Spacer()
                Text("Symbol names are SF Symbols (e.g. terminal, safari, music.note).").font(.system(size: 11)).foregroundStyle(pal.text3)
            }
        }
    }
}

// MARK: - Pointer & scroll pane

struct PointerPane: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var permissions: Permissions
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Toggle(isOn: $settings.managePointer) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Manage pointer settings for this mouse").font(.system(size: 13, weight: .semibold))
                    Text("Applies to the Surface Mouse only; the trackpad and other mice keep the system values. Re-applied whenever the mouse reconnects.")
                        .font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch)
            Group {
                HStack {
                    Text("Tracking speed").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.pointerSpeed, in: 0...3, step: 0.125) { } minimumValueLabel: { Text("Slow").font(.system(size: 10)) } maximumValueLabel: { Text("Fast").font(.system(size: 10)) }
                    Text(String(format: "%.2f", settings.pointerSpeed)).font(.system(size: 11, design: .monospaced)).frame(width: 40)
                }
                Toggle(isOn: $settings.noAcceleration) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("No pointer acceleration").font(.system(size: 12))
                        Text("1:1 sensor movement, like a gaming mouse setting. Overrides the speed slider.").font(.system(size: 11)).foregroundStyle(pal.text2)
                    }
                }
                HStack {
                    Text("Scrolling speed").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.scrollSpeed, in: 0.05...1.5, step: 0.05) { } minimumValueLabel: { Text("Slow").font(.system(size: 10)) } maximumValueLabel: { Text("Fast").font(.system(size: 10)) }
                    Text(String(format: "%.2f", settings.scrollSpeed)).font(.system(size: 11, design: .monospaced)).frame(width: 40)
                }
            }.disabled(!settings.managePointer).opacity(settings.managePointer ? 1 : 0.5)
            Divider()
            Toggle(isOn: $settings.reverseScroll) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Reverse scrolling direction for this mouse").font(.system(size: 13, weight: .medium))
                    Text("Flips wheel scrolls only, so the trackpad keeps natural scrolling. Needs Accessibility.").font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch).disabled(!permissions.accessibility)
            Divider()
            Text("Wheel feel").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
            Toggle(isOn: $settings.smoothScroll) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Smooth kinetic scrolling (Magic Mouse / Windows feel)").font(.system(size: 12))
                    Text("Each notch becomes a pixel glide instead of a jump; a fast spin coasts on after the wheel stops. Needs Accessibility.").font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch).disabled(!permissions.accessibility)
            Group {
                HStack {
                    Text("Distance per notch").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.scrollPixelsPerNotch, in: 20...160, step: 5)
                    Text("\(Int(settings.scrollPixelsPerNotch)) px").font(.system(size: 11, design: .monospaced)).frame(width: 50)
                }
                HStack {
                    Text("Glide").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.scrollGlide, in: 1...10, step: 1) { } minimumValueLabel: { Text("Snappy").font(.system(size: 10)) } maximumValueLabel: { Text("Long").font(.system(size: 10)) }
                    Text("\(Int(settings.scrollGlide))").font(.system(size: 11, design: .monospaced)).frame(width: 50)
                }
                HStack {
                    Text("Momentum").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.scrollMomentum, in: 0...10, step: 1) { } minimumValueLabel: { Text("Off").font(.system(size: 10)) } maximumValueLabel: { Text("Coast").font(.system(size: 10)) }
                    Text("\(Int(settings.scrollMomentum))").font(.system(size: 11, design: .monospaced)).frame(width: 50)
                }
                HStack {
                    Text("Acceleration").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.scrollAccel, in: 0...3, step: 0.25) { } minimumValueLabel: { Text("Linear").font(.system(size: 10)) } maximumValueLabel: { Text("Fast").font(.system(size: 10)) }
                    Text(String(format: "%.2f×", 1 + settings.scrollAccel)).font(.system(size: 11, design: .monospaced)).frame(width: 50)
                }
            }.disabled(!settings.smoothScroll || !permissions.accessibility).opacity(settings.smoothScroll ? 1 : 0.5)
            Toggle(isOn: Binding(get: { settings.rule(.scrollStep).enabled }, set: { v in var r = settings.rule(.scrollStep); r.enabled = v; settings.setRule(.scrollStep, r) })) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Ratchet tick per notch (MX feel)").font(.system(size: 12))
                    Text("A detent pulse for every notch, so the free-spinning wheel feels notched. Waveform and strength are in the Haptics tab.").font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch)
            Toggle(isOn: $settings.ratchetDynamic) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Dynamic ratchet").font(.system(size: 12))
                    Text("Firm detents when you scroll slowly, light quick ticks when you scroll fast, a firmer bump when you reverse.").font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch).disabled(!settings.rule(.scrollStep).enabled)
            HStack {
                Text("Go light above").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                Slider(value: $settings.ratchetLightRate, in: 4...30, step: 1)
                Text("\(Int(settings.ratchetLightRate))/s").font(.system(size: 11, design: .monospaced)).frame(width: 50)
            }.disabled(!settings.ratchetDynamic || !settings.rule(.scrollStep).enabled).opacity(settings.ratchetDynamic ? 1 : 0.5)
            Toggle(isOn: $settings.freeSpin) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Free-spin on a flick (SmartShift)").font(.system(size: 12))
                    Text("Spin fast and the wheel switches to free-spin: no ticks, boosted distance, one Collide pulse on the switch.").font(.system(size: 11)).foregroundStyle(pal.text2)
                }
            }.toggleStyle(.switch)
            Group {
                HStack {
                    Text("Flick threshold").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.freeSpinThreshold, in: 8...40, step: 1)
                    Text("\(Int(settings.freeSpinThreshold))/s").font(.system(size: 11, design: .monospaced)).frame(width: 50)
                }
                HStack {
                    Text("Free-spin boost").font(.system(size: 12)).frame(width: 130, alignment: .leading)
                    Slider(value: $settings.freeSpinMultiplier, in: 1.5...6, step: 0.5)
                    Text(String(format: "%.1f×", settings.freeSpinMultiplier)).font(.system(size: 11, design: .monospaced)).frame(width: 50)
                }
            }.disabled(!settings.freeSpin).opacity(settings.freeSpin ? 1 : 0.5)
            Divider()
            HStack {
                Text("Double-click speed, secondary click side and the system-wide tracking speed live in System Settings.")
                    .font(.system(size: 11)).foregroundStyle(pal.text2)
                Spacer()
                Button("Open System Settings › Mouse") {
                    if let u = URL(string: "x-apple.systempreferences:com.apple.Mouse-Settings.extension") { NSWorkspace.shared.open(u) }
                }
            }
        }
    }
}

// MARK: - Apps pane (per-app profiles)

struct AppsPane: View {
    @EnvironmentObject var settings: Settings
    @Environment(\.palette) private var pal
    @State private var selected: UUID?

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 3) {
                    SectionTitle(text: "Per-app profiles", pal: pal)
                    Text("Overrides that apply while an app is frontmost: a different click or wheel feel, haptics off, its own ring, its own gestures. "
                       + "Anything left on Default follows the main tabs.")
                        .font(.system(size: 11)).foregroundStyle(pal.text2)
                }
                Spacer()
                Menu {
                    ForEach(runningApps, id: \.bundleIdentifier) { app in
                        Button { add(bundleID: app.bundleIdentifier ?? "", name: app.localizedName ?? "App") } label: {
                            Label(app.localizedName ?? "App", systemImage: "app")
                        }
                    }
                    Divider()
                    Button("Choose an app…") { choose() }
                } label: { Label("Add app", systemImage: "plus") }
                .menuStyle(.borderedButton).frame(width: 120)
            }
            Divider()
            if settings.profiles.isEmpty {
                Text("No profiles yet. Add the apps you want to feel different: a design tool with a fine wheel, a game with haptics off, a DAW with its own ring.")
                    .font(.system(size: 12)).foregroundStyle(pal.text2).padding(.vertical, 20)
            } else {
                HStack(alignment: .top, spacing: 16) {
                    VStack(spacing: 4) {
                        ForEach(settings.profiles) { p in
                            let on = p.id == selected
                            Button { selected = p.id } label: {
                                HStack(spacing: 8) {
                                    Image(nsImage: icon(for: p.bundleID)).resizable().frame(width: 22, height: 22)
                                    VStack(alignment: .leading, spacing: 0) {
                                        Text(p.name).font(.system(size: 12, weight: .medium)).foregroundStyle(pal.text).lineLimit(1)
                                        Text(p.bundleID).font(.system(size: 9)).foregroundStyle(pal.text3).lineLimit(1)
                                    }
                                    Spacer()
                                    if settings.frontBundleID == p.bundleID { Circle().fill(Color.green).frame(width: 6, height: 6) }
                                }
                                .padding(.horizontal, 8).padding(.vertical, 6)
                                .background(RoundedRectangle(cornerRadius: 9, style: .continuous).fill(on ? pal.accent.opacity(0.18) : Color.clear))
                                .contentShape(Rectangle())
                            }.buttonStyle(.plain)
                        }
                    }
                    .frame(width: 230)
                    if let i = settings.profiles.firstIndex(where: { $0.id == selected }) {
                        ProfileEditor(profile: $settings.profiles[i]) {
                            settings.profiles.remove(at: i); selected = nil
                        }
                    } else {
                        Text("Select an app on the left.").font(.system(size: 12)).foregroundStyle(pal.text3).padding(.top, 8)
                    }
                }
            }
        }
        .onAppear { if selected == nil { selected = settings.profiles.first?.id } }
    }

    private var runningApps: [NSRunningApplication] {
        NSWorkspace.shared.runningApplications
            .filter { $0.activationPolicy == .regular && $0.bundleIdentifier != nil && $0.bundleIdentifier != Bundle.main.bundleIdentifier }
            .sorted { ($0.localizedName ?? "") < ($1.localizedName ?? "") }
    }

    private func icon(for bundleID: String) -> NSImage {
        if let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: bundleID) { return NSWorkspace.shared.icon(forFile: url.path) }
        return NSImage(systemSymbolName: "app", accessibilityDescription: nil) ?? NSImage()
    }

    private func add(bundleID: String, name: String) {
        guard !bundleID.isEmpty else { return }
        if let existing = settings.profiles.first(where: { $0.bundleID == bundleID }) { selected = existing.id; return }
        let p = AppProfile(bundleID: bundleID, name: name)
        settings.profiles.append(p); selected = p.id
    }

    private func choose() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.applicationBundle]
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        guard panel.runModal() == .OK, let url = panel.url, let b = Bundle(url: url), let id = b.bundleIdentifier else { return }
        let name = (b.infoDictionary?["CFBundleDisplayName"] as? String) ?? (b.infoDictionary?["CFBundleName"] as? String) ?? url.deletingPathExtension().lastPathComponent
        add(bundleID: id, name: name)
    }
}

struct ProfileEditor: View {
    @Binding var profile: AppProfile
    let remove: () -> Void
    @EnvironmentObject var settings: Settings
    @Environment(\.palette) private var pal

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(profile.name).font(.system(size: 14, weight: .semibold)).foregroundStyle(pal.text)
                Spacer()
                Button(role: .destructive) { remove() } label: { Label("Remove", systemImage: "trash") }
            }
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 8) {
                GridRow {
                    Text("Haptics").font(.system(size: 12)).foregroundStyle(pal.text2)
                    tri($profile.hapticsEnabled)
                    Text("Smooth scrolling").font(.system(size: 12)).foregroundStyle(pal.text2)
                    tri($profile.smoothScroll)
                }
                GridRow {
                    Text("Free-spin").font(.system(size: 12)).foregroundStyle(pal.text2)
                    tri($profile.freeSpin)
                    Text("Dynamic ratchet").font(.system(size: 12)).foregroundStyle(pal.text2)
                    tri($profile.ratchetDynamic)
                }
                GridRow {
                    Text("Left click").font(.system(size: 12)).foregroundStyle(pal.text2)
                    clickPicker(right: false)
                    Text("Right click").font(.system(size: 12)).foregroundStyle(pal.text2)
                    clickPicker(right: true)
                }
                GridRow {
                    Text("Wheel").font(.system(size: 12)).foregroundStyle(pal.text2)
                    wheelPicker()
                    Text("").gridCellColumns(2)
                }
            }
            Divider()
            Toggle(isOn: Binding(get: { profile.gestureUp != nil }, set: { on in
                if on { profile.gestureUp = settings.gestureUp; profile.gestureDown = settings.gestureDown; profile.gestureLeft = settings.gestureLeft; profile.gestureRight = settings.gestureRight }
                else { profile.gestureUp = nil; profile.gestureDown = nil; profile.gestureLeft = nil; profile.gestureRight = nil }
            })) { Text("Own hold-and-move gestures").font(.system(size: 12, weight: .medium)) }.toggleStyle(.switch)
            if profile.gestureUp != nil {
                Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 6) {
                    GridRow { Text("Up").font(.system(size: 11)).foregroundStyle(pal.text2); ActionPicker(action: Binding(get: { profile.gestureUp ?? .none }, set: { profile.gestureUp = $0 })) }
                    GridRow { Text("Down").font(.system(size: 11)).foregroundStyle(pal.text2); ActionPicker(action: Binding(get: { profile.gestureDown ?? .none }, set: { profile.gestureDown = $0 })) }
                    GridRow { Text("Left").font(.system(size: 11)).foregroundStyle(pal.text2); ActionPicker(action: Binding(get: { profile.gestureLeft ?? .none }, set: { profile.gestureLeft = $0 })) }
                    GridRow { Text("Right").font(.system(size: 11)).foregroundStyle(pal.text2); ActionPicker(action: Binding(get: { profile.gestureRight ?? .none }, set: { profile.gestureRight = $0 })) }
                }
            }
            Divider()
            Toggle(isOn: Binding(get: { profile.ringItems != nil }, set: { on in profile.ringItems = on ? settings.ringItems : nil })) {
                Text("Own Action Ring").font(.system(size: 12, weight: .medium))
            }.toggleStyle(.switch)
            if profile.ringItems != nil {
                RingEditor(items: Binding(get: { profile.ringItems ?? [] }, set: { profile.ringItems = $0 }))
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    /// Default / On / Off for an optional Bool.
    private func tri(_ b: Binding<Bool?>) -> some View {
        Picker("", selection: Binding(get: { b.wrappedValue.map { $0 ? 1 : 2 } ?? 0 }, set: { b.wrappedValue = $0 == 0 ? nil : ($0 == 1) })) {
            Text("Default").tag(0); Text("On").tag(1); Text("Off").tag(2)
        }.pickerStyle(.segmented).labelsHidden().frame(width: 170)
    }

    private func clickPicker(right: Bool) -> some View {
        let d = right ? HapticEvent.rightDown : .leftDown, u = right ? HapticEvent.rightUp : .leftUp, h = right ? HapticEvent.rightHold : .leftHold
        let current = profile.rules[d.rawValue] == nil ? "Default"
            : (ClickFeel.all.first { profile.rules[d.rawValue] == $0.down && profile.rules[u.rawValue] == $0.up && profile.rules[h.rawValue] == $0.hold }?.name ?? "Custom")
        return Picker("", selection: Binding(get: { current }, set: { n in
            if n == "Default" { profile.rules[d.rawValue] = nil; profile.rules[u.rawValue] = nil; profile.rules[h.rawValue] = nil }
            else if let f = ClickFeel.all.first(where: { $0.name == n }) { profile.rules[d.rawValue] = f.down; profile.rules[u.rawValue] = f.up; profile.rules[h.rawValue] = f.hold }
        })) {
            Text("Default").tag("Default")
            ForEach(ClickFeel.all) { Text($0.name).tag($0.name) }
            if current == "Custom" { Text("Custom").tag("Custom") }
        }.labelsHidden().frame(width: 170)
    }

    private func wheelPicker() -> some View {
        let current = profile.rules[HapticEvent.scrollStep.rawValue] == nil ? "Default"
            : (WheelFeel.all.first { profile.rules[HapticEvent.scrollStep.rawValue] == $0.step && profile.scrollStepEvery == $0.every && profile.ratchetDynamic == $0.dynamic
                && profile.ratchetLightRate == $0.lightRate && profile.freeSpin == $0.freeSpin && profile.rules[HapticEvent.freeSpinTexture.rawValue] == $0.texture && profile.freeSpinTextureEvery == $0.textureEvery }?.name ?? "Custom")
        return Picker("", selection: Binding(get: { current }, set: { n in
            if n == "Default" {
                profile.rules[HapticEvent.scrollStep.rawValue] = nil; profile.rules[HapticEvent.freeSpinTexture.rawValue] = nil
                profile.scrollStepEvery = nil; profile.ratchetDynamic = nil; profile.ratchetLightRate = nil; profile.freeSpin = nil; profile.freeSpinTextureEvery = nil
            } else if let f = WheelFeel.all.first(where: { $0.name == n }) {
                profile.rules[HapticEvent.scrollStep.rawValue] = f.step; profile.rules[HapticEvent.freeSpinTexture.rawValue] = f.texture
                profile.scrollStepEvery = f.every; profile.ratchetDynamic = f.dynamic; profile.ratchetLightRate = f.lightRate; profile.freeSpin = f.freeSpin; profile.freeSpinTextureEvery = f.textureEvery
            }
        })) {
            Text("Default").tag("Default")
            ForEach(WheelFeel.all) { Text($0.name).tag($0.name) }
            if current == "Custom" { Text("Custom").tag("Custom") }
        }.labelsHidden().frame(width: 170)
    }
}

/// The local plugin bridge: status, port, token.
struct BridgeSection: View {
    @EnvironmentObject var settings: Settings
    @Environment(\.palette) private var pal
    @State private var copied = false

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                SectionTitle(text: "Plugin bridge", pal: pal)
                Spacer()
                let running = AppDelegate.shared?.bridge.running ?? false
                Text(running ? "listening on 127.0.0.1:\(settings.bridgePort)" : "off").font(.system(size: 11, design: .monospaced)).foregroundStyle(running ? .green : pal.text3)
                Toggle("", isOn: $settings.bridgeEnabled).toggleStyle(.switch).labelsHidden()
            }
            Text("App plugins (Photoshop UXP, VS Code, scripts) can request a pulse by name over a loopback HTTP call; the plugin decides when, iXpat plays it under the \u{201c}App plugin pulse\u{201d} rule. Sample plugin and curl example in the repo's plugins folder.")
                .font(.system(size: 11)).foregroundStyle(pal.text2)
            HStack(spacing: 10) {
                Text("Token").font(.system(size: 11)).foregroundStyle(pal.text2)
                Text(settings.bridgeToken).font(.system(size: 11, design: .monospaced)).foregroundStyle(pal.text).textSelection(.enabled)
                Button(copied ? "Copied" : "Copy") {
                    NSPasteboard.general.clearContents(); NSPasteboard.general.setString(settings.bridgeToken, forType: .string)
                    copied = true; DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { copied = false }
                }
                Button("New token") { settings.bridgeToken = Settings.newToken(); AppDelegate.shared?.bridge.start(port: UInt16(clamping: settings.bridgePort), token: settings.bridgeToken) }
                Spacer()
                Text("Port").font(.system(size: 11)).foregroundStyle(pal.text2)
                TextField("47337", value: $settings.bridgePort, format: .number).textFieldStyle(.roundedBorder).frame(width: 70)
                    .onSubmit { AppDelegate.shared?.bridge.start(port: UInt16(clamping: settings.bridgePort), token: settings.bridgeToken) }
            }
        }
    }
}

// MARK: - Diagnostics pane

struct DiagnosticsPane: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var permissions: Permissions
    @Environment(\.palette) private var pal
    @State private var featureDump = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Permissions").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
            ForEach(PermissionKind.allCases) { kind in
                HStack(alignment: .top, spacing: 12) {
                    Image(systemName: permissions.granted(kind) ? "checkmark.circle.fill" : "circle")
                        .foregroundStyle(permissions.granted(kind) ? .green : pal.text3)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(kind.title).font(.system(size: 12, weight: .medium)).foregroundStyle(pal.text)
                        Text(kind.why).font(.system(size: 11)).foregroundStyle(pal.text2)
                        if !permissions.granted(kind) {
                            Text("If macOS already lists iXpat as allowed, the grant is stale: run  \(permissions.resetCommand(kind))  and relaunch.")
                                .font(.system(size: 10)).foregroundStyle(pal.text3)
                        }
                    }
                    Spacer()
                    if !permissions.granted(kind) {
                        Button("Allow…") { permissions.prompt(kind) }.buttonStyle(.borderedProminent)
                        Button("Open Settings") { permissions.openSettings(kind) }
                    }
                }
            }
            Divider()
            BridgeSection()
            Divider()
            Text("Device").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 4) {
                row("State", device.connected ? "connected" : (device.openRefused ? "found, but open refused (Input Monitoring)" : "not found"))
                row("Vendor / Product", String(format: "0x%04X / 0x%04X", MouseDevice.vendorID, MouseDevice.productID))
                row("Firmware", device.firmware)
                row("Serial", device.serial)
                row("Transport", device.transport)
                row("Reports seen", "\(device.reportsSeen)")
                row("Last error", device.lastError.isEmpty ? "none" : device.lastError)
            }
            HStack {
                Button("Read haptic table (0x42)") { device.readHapticTable() }
                Button("Dump vendor feature 0x24") { dump(0x24) }
                Button("Dump vendor feature 0x2A") { dump(0x2A) }
                Button("Copy report") { copyReport() }
            }.disabled(!device.connected)
            if !featureDump.isEmpty {
                Text(featureDump).font(.system(size: 10, design: .monospaced)).foregroundStyle(pal.text2).textSelection(.enabled)
            }
            Divider()
            HStack {
                Text("Raw input reports (buttons, keys, vendor; motion is not logged)").font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
                Spacer()
                Button("Clear") { device.clearLog() }
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(device.log.suffix(60).reversed()) { r in
                        HStack(spacing: 10) {
                            Text(r.time.formatted(date: .omitted, time: .standard)).foregroundStyle(pal.text3)
                            Text(r.kind).frame(width: 90, alignment: .leading).foregroundStyle(pal.accent)
                            Text(r.hex).foregroundStyle(pal.text)
                        }.font(.system(size: 10, design: .monospaced))
                    }
                }.frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(height: 180)
            .padding(8)
            .background(RoundedRectangle(cornerRadius: 8).fill(pal.field))
        }
    }

    private func row(_ k: String, _ v: String) -> some View {
        GridRow {
            Text(k).font(.system(size: 11)).foregroundStyle(pal.text2)
            Text(v).font(.system(size: 11, design: .monospaced)).foregroundStyle(pal.text).textSelection(.enabled)
        }
    }

    private func dump(_ id: UInt8) {
        device.readFeatureAsync(id) { bytes in
            guard let bytes else { featureDump = String(format: "feature 0x%02x: read failed", id); return }
            featureDump = String(format: "feature 0x%02x (%d bytes): ", id, bytes.count) + bytes.map { String(format: "%02x", $0) }.joined(separator: " ")
        }
    }

    private func copyReport() {
        var s = "iXpat diagnostics\n"
        s += "connected=\(device.connected) fw=\(device.firmware) transport=\(device.transport) reports=\(device.reportsSeen)\n"
        s += "inputMonitoring=\(permissions.inputMonitoring) accessibility=\(permissions.accessibility)\n"
        s += "waveforms: " + device.waveforms.map { "\($0.ordinal)=\($0.name)/\($0.durationMs)ms" }.joined(separator: ", ") + "\n"
        s += device.log.suffix(60).map { "\($0.time.formatted(date: .omitted, time: .standard)) \($0.kind) \($0.hex)" }.joined(separator: "\n")
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(s, forType: .string)
    }
}

import SwiftUI
import ServiceManagement

/// The menu-bar popover: a frosted card with the device state, two quick
/// switches, and the handful of actions worth reaching for without the window.
struct MenuView: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var engine: HapticEngine
    @EnvironmentObject var permissions: Permissions
    @Environment(\.colorScheme) private var systemScheme
    @State private var launchAtLogin = SMAppService.mainApp.status == .enabled
    @State private var pulse = false

    private var pal: Palette { Palette.resolve(theme: settings.theme, systemScheme: systemScheme, accent: settings.accent) }

    var body: some View {
        let pal = pal
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                Image(nsImage: NSApp.applicationIconImage).resizable().interpolation(.high).frame(width: 30, height: 30)
                    .shadow(color: pal.accent.opacity(0.4), radius: 6, y: 2)
                VStack(alignment: .leading, spacing: 1) {
                    Wordmark(pal: pal, size: 15)
                    HStack(spacing: 6) {
                        Circle().fill(device.connected ? Color.green : pal.text3).frame(width: 7, height: 7)
                            .overlay(Circle().stroke(Color.green.opacity(device.connected ? 0.5 : 0), lineWidth: pulse ? 5 : 1).opacity(pulse ? 0 : 1))
                            .animation(device.connected ? .easeOut(duration: 1.4).repeatForever(autoreverses: false) : .default, value: pulse)
                        Text(device.connected ? "Connected" : "Not connected")
                            .font(.system(size: 11)).foregroundStyle(pal.text2)
                    }
                }
                Spacer()
                if device.connected { BatteryBadge(percent: device.battery) }
                Toggle("", isOn: $settings.enabled).toggleStyle(.switch).labelsHidden().controlSize(.small)
            }
            .padding(12)
            .glass(pal, radius: 14, tint: pal.accent)

            if !permissions.inputMonitoring {
                Button { permissions.prompt(.inputMonitoring) } label: {
                    Label("Allow Input Monitoring to read the mouse", systemImage: "exclamationmark.triangle.fill").font(.system(size: 11))
                }.buttonStyle(.plain).foregroundStyle(.orange).padding(.horizontal, 4)
            }

            VStack(spacing: 0) {
                switchRow("Haptics", "dot.radiowaves.left.and.right", $settings.hapticsEnabled)
                Divider().padding(.leading, 40)
                switchRow("Reverse scrolling", "arrow.up.arrow.down", $settings.reverseScroll, enabled: permissions.accessibility)
                Divider().padding(.leading, 40)
                switchRow("Start at login", "sunrise", Binding(get: { launchAtLogin }, set: { on in
                    do { if on { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() } } catch {}
                    launchAtLogin = SMAppService.mainApp.status == .enabled
                }))
            }
            .glass(pal, radius: 14)

            VStack(spacing: 0) {
                actionRow("Open Action Ring", "circle.hexagongrid", enabled: permissions.accessibility) {
                    (NSApp.delegate as? AppDelegate)?.run(.actionRing, hold: false)
                }
                Divider().padding(.leading, 40)
                actionRow("Test pulse", "waveform.path", enabled: device.connected) {
                    let ord = Waveform.ordinal(for: 0x1015, in: device.waveforms) ?? 7
                    engine.play(ordinal: ord, intensity: 4, label: "Test")
                }
                Divider().padding(.leading, 40)
                actionRow("Settings…", "slider.horizontal.3", shortcut: ",") { (NSApp.delegate as? AppDelegate)?.showSettings() }
            }
            .glass(pal, radius: 14)

            HStack {
                Text(engine.lastEvent == "—" ? "Ready" : engine.lastEvent).font(.system(size: 10)).foregroundStyle(pal.text3).lineLimit(1)
                Spacer()
                Button { NSApp.terminate(nil) } label: {
                    Label("Quit", systemImage: "power").font(.system(size: 11, weight: .medium)).foregroundStyle(pal.text2)
                }.buttonStyle(.plain).keyboardShortcut("q")
            }
            .padding(.horizontal, 6)
        }
        .padding(12)
        .frame(width: 300)
        .background(Backdrop(pal: pal))
        .environment(\.palette, pal)
        .preferredColorScheme(settings.theme == .system ? nil : (pal.isDark ? .dark : .light))
        .tint(pal.accent)
        .onAppear { pulse = true }
    }

    private func icon(_ name: String, _ pal: Palette) -> some View {
        Image(systemName: name).font(.system(size: 12, weight: .semibold)).foregroundStyle(pal.accent)
            .frame(width: 24, height: 24)
            .background(RoundedRectangle(cornerRadius: 7, style: .continuous).fill(pal.accent.opacity(0.16)))
    }

    private func switchRow(_ title: String, _ symbol: String, _ on: Binding<Bool>, enabled: Bool = true) -> some View {
        HStack(spacing: 10) {
            icon(symbol, pal)
            Text(title).font(.system(size: 12, weight: .medium)).foregroundStyle(pal.text)
            Spacer()
            Toggle("", isOn: on).toggleStyle(.switch).labelsHidden().controlSize(.small)
        }
        .padding(.horizontal, 10).padding(.vertical, 7)
        .opacity(enabled ? 1 : 0.5).disabled(!enabled)
    }

    private func actionRow(_ title: String, _ symbol: String, enabled: Bool = true, shortcut: KeyEquivalent? = nil, _ run: @escaping () -> Void) -> some View {
        let b = Button(action: run) {
            HStack(spacing: 10) {
                icon(symbol, pal)
                Text(title).font(.system(size: 12, weight: .medium)).foregroundStyle(pal.text)
                Spacer()
                Image(systemName: "chevron.right").font(.system(size: 10, weight: .semibold)).foregroundStyle(pal.text3)
            }
            .padding(.horizontal, 10).padding(.vertical, 7)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .opacity(enabled ? 1 : 0.5).disabled(!enabled)
        return Group {
            if let shortcut { b.keyboardShortcut(shortcut) } else { b }
        }
    }
}

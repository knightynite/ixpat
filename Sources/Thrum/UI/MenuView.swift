import SwiftUI
import ServiceManagement

struct MenuView: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var engine: HapticEngine
    @EnvironmentObject var permissions: Permissions
    @State private var launchAtLogin = SMAppService.mainApp.status == .enabled

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                Image(nsImage: NSApp.applicationIconImage).resizable().interpolation(.high).frame(width: 22, height: 22)
                Text("Thrum").font(.system(size: 13, weight: .bold))
                Spacer()
                Toggle("", isOn: $settings.enabled).toggleStyle(.switch).labelsHidden()
            }
            HStack(spacing: 6) {
                Circle().fill(device.connected ? Color.green : Color.secondary).frame(width: 7, height: 7)
                Text(device.connected ? "Surface Mouse connected" : "Surface Mouse not connected")
                    .font(.system(size: 12)).foregroundStyle(.secondary)
                Spacer()
                if device.connected { BatteryBadge(percent: device.battery) }
            }
            if !permissions.inputMonitoring {
                Button { permissions.prompt(.inputMonitoring) } label: {
                    Label("Allow Input Monitoring to read the mouse", systemImage: "exclamationmark.triangle.fill")
                        .font(.system(size: 11))
                }.buttonStyle(.plain).foregroundStyle(.orange)
            }
            Divider()
            Toggle(isOn: $settings.hapticsEnabled) { Text("Haptics").font(.system(size: 12)) }.toggleStyle(.checkbox)
            Toggle(isOn: $settings.reverseScroll) { Text("Reverse scrolling (this mouse only)").font(.system(size: 12)) }
                .toggleStyle(.checkbox).disabled(!permissions.accessibility)
            Button {
                (NSApp.delegate as? AppDelegate)?.run(.actionRing, hold: false)
            } label: { Label("Open Action Ring", systemImage: "circle.hexagongrid").font(.system(size: 12)) }
                .disabled(!permissions.accessibility)
            Button {
                let ord = Waveform.ordinal(for: 0x1015, in: device.waveforms) ?? 7
                engine.play(ordinal: ord, intensity: 4, label: "Test")
            } label: { Label("Test pulse", systemImage: "dot.radiowaves.left.and.right").font(.system(size: 12)) }
                .disabled(!device.connected)
            Divider()
            Button { (NSApp.delegate as? AppDelegate)?.showSettings() } label: {
                Label("Settings…", systemImage: "slider.horizontal.3").font(.system(size: 12))
            }.keyboardShortcut(",")
            Toggle(isOn: Binding(get: { launchAtLogin }, set: { on in
                do { if on { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() } } catch {}
                launchAtLogin = SMAppService.mainApp.status == .enabled
            })) { Text("Start at login").font(.system(size: 12)) }.toggleStyle(.checkbox)
            Button { NSApp.terminate(nil) } label: { Label("Quit Thrum", systemImage: "power").font(.system(size: 12)) }
                .keyboardShortcut("q")
        }
        .padding(12)
        .frame(width: 290)
    }
}

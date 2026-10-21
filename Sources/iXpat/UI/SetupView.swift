import SwiftUI
import AppKit

/// First-run guide: pair the mouse, then grant the two permissions iXpat needs.
/// Shown on first launch and on any later launch where a permission is missing
/// (unless the user ticked "Don't show this again"); always reachable from the menu.
struct SetupView: View {
    @EnvironmentObject var settings: Settings
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var permissions: Permissions
    @Environment(\.colorScheme) private var systemScheme
    @AppStorage("setupSuppressed") private var suppressed = false

    private var pal: Palette { Palette.resolve(theme: settings.theme, systemScheme: systemScheme, accent: settings.accent) }
    private var allGranted: Bool { permissions.inputMonitoring && permissions.accessibility }

    var body: some View {
        let pal = pal
        VStack(spacing: 18) {
            VStack(spacing: 6) {
                Image(nsImage: NSApp.applicationIconImage).resizable().interpolation(.high).frame(width: 64, height: 64)
                    .shadow(color: pal.accent.opacity(0.4), radius: 12, y: 3)
                Wordmark(pal: pal, size: 26)
                Text("Three steps and the mouse comes alive.")
                    .font(.system(size: 13)).foregroundStyle(pal.text2)
            }

            VStack(spacing: 10) {
                step(1, "Pair the Surface Mouse",
                     done: device.connected,
                     doneText: "Connected.",
                     todo: "Hold the power and pairing button on the underside until the light pulses, then connect it in Bluetooth settings.",
                     button: "Open Bluetooth") { open("x-apple.systempreferences:com.apple.BluetoothSettings") }
                step(2, "Allow Input Monitoring",
                     done: permissions.inputMonitoring,
                     doneText: "Granted. iXpat can read clicks, the wheel and the action button.",
                     todo: "Lets iXpat read the mouse's own reports: clicks for haptics, the scroll wheel and the action button. Without it the mouse stays silent.",
                     button: permissions.attempted(.inputMonitoring) ? "Open System Settings" : "Allow…") { permissions.prompt(.inputMonitoring) }
                step(3, "Allow Accessibility",
                     done: permissions.accessibility,
                     doneText: "Granted. Scrolling, the action button and the ticks are all on.",
                     todo: "Needed for kinetic and reversed scrolling, to hide the action button's shortcut from apps, and for menu and text-selection ticks.",
                     button: permissions.attempted(.accessibility) ? "Open System Settings" : "Allow…") { permissions.prompt(.accessibility) }
            }

            Text(allGranted
                 ? "All set. Click something."
                 : "macOS shows each permission once. If a prompt did not appear, use Open System Settings, turn iXpat on in the list, and come back here; this window updates by itself.")
                .font(.system(size: 11)).foregroundStyle(allGranted ? pal.text : pal.text2)
                .multilineTextAlignment(.center).fixedSize(horizontal: false, vertical: true)

            HStack {
                Toggle("Don't show this again", isOn: $suppressed).toggleStyle(.checkbox).font(.system(size: 11)).foregroundStyle(pal.text2)
                Spacer()
                Button(allGranted ? "Open iXpat" : "Skip for now") {
                    AppDelegate.shared?.finishSetup()
                }
                .keyboardShortcut(.defaultAction)
                .buttonStyle(.borderedProminent)
            }
            .padding(.top, 2)
        }
        .padding(.horizontal, 26).padding(.top, 30).padding(.bottom, 20)
        .frame(width: 470)
        .background(Backdrop(pal: pal))
        .environment(\.palette, pal)
        .preferredColorScheme(settings.theme == .system ? nil : (pal.isDark ? .dark : .light))
        .tint(pal.accent)
    }

    private func step(_ n: Int, _ title: String, done: Bool, doneText: String, todo: String,
                      button: String, action: @escaping () -> Void) -> some View {
        let pal = pal
        return HStack(alignment: .top, spacing: 12) {
            ZStack {
                Circle().fill(done ? Color.green.opacity(0.18) : pal.accent.opacity(0.16)).frame(width: 28, height: 28)
                if done {
                    Image(systemName: "checkmark").font(.system(size: 12, weight: .bold)).foregroundStyle(.green)
                } else {
                    Text("\(n)").font(.system(size: 12, weight: .bold)).foregroundStyle(pal.accent)
                }
            }
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
                Text(done ? doneText : todo).font(.system(size: 11)).foregroundStyle(pal.text2)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 8)
            if !done {
                Button(button, action: action).controlSize(.small)
            }
        }
        .padding(12)
        .glass(pal, radius: 12, tint: done ? Color.green : nil)
    }

    private func open(_ s: String) {
        if let u = URL(string: s) { NSWorkspace.shared.open(u) }
    }
}

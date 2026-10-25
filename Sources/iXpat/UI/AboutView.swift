import SwiftUI

enum AboutLinks {
    static let site = URL(string: "https://knightynite.github.io/ixpat/")!
    static let source = URL(string: "https://github.com/knightynite/ixpat")!
    static let issues = URL(string: "https://github.com/knightynite/ixpat/issues")!
    static let author = URL(string: "https://www.linkedin.com/in/alnnajafi/")!
}

/// The About window: version, credit, links and the update controls.
struct AboutView: View {
    @EnvironmentObject var settings: Settings
    @Environment(\.colorScheme) private var systemScheme

    private var pal: Palette { Palette.resolve(theme: settings.theme, systemScheme: systemScheme, accent: settings.accent) }

    static let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
    static let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"
    static var arch: String {
        #if arch(arm64)
        return "Apple Silicon"
        #else
        return "Intel"
        #endif
    }

    var body: some View {
        let pal = pal
        VStack(spacing: 14) {
            Image(nsImage: NSApp.applicationIconImage).resizable().interpolation(.high).frame(width: 88, height: 88)
                .shadow(color: pal.accent.opacity(0.45), radius: 14, y: 4)
            VStack(spacing: 4) {
                Wordmark(pal: pal, size: 30)
                Text("Version \(Self.version) (\(Self.build)) · \(Self.arch)")
                    .font(.system(size: 11)).foregroundStyle(pal.text2).textSelection(.enabled)
            }
            Text("Haptics, an Action Ring, kinetic scrolling and force click for the Microsoft Surface Mouse (2nd Edition) on macOS.")
                .font(.system(size: 12)).foregroundStyle(pal.text)
                .multilineTextAlignment(.center).fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 16) {
                Link("Website", destination: AboutLinks.site)
                Link("Source code", destination: AboutLinks.source)
                Link("Report an issue", destination: AboutLinks.issues)
            }
            .font(.system(size: 11, weight: .medium))
            HStack(spacing: 10) {
                Toggle("Check for updates daily", isOn: $settings.autoCheckUpdates)
                    .toggleStyle(.switch).controlSize(.small).font(.system(size: 11)).foregroundStyle(pal.text)
                Spacer()
                Button("Check Now") { Updater.shared.check(manual: true) }.controlSize(.small)
            }
            .padding(10)
            .glass(pal, radius: 12)
            Text("Free and open source under the MIT License. Not affiliated with or endorsed by Microsoft. Surface is a trademark of Microsoft Corporation.")
                .font(.system(size: 9)).foregroundStyle(pal.text3)
                .multilineTextAlignment(.center).fixedSize(horizontal: false, vertical: true)
        }
        .padding(.horizontal, 26).padding(.top, 34).padding(.bottom, 20)
        .frame(width: 380)
        .background(Backdrop(pal: pal))
        .environment(\.palette, pal)
        .preferredColorScheme(settings.theme == .system ? nil : (pal.isDark ? .dark : .light))
        .tint(pal.accent)
    }
}

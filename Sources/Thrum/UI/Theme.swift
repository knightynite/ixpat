import SwiftUI

extension Color {
    init(rgb: UInt, _ alpha: Double = 1) {
        self.init(.sRGB,
                  red: Double((rgb >> 16) & 0xff) / 255,
                  green: Double((rgb >> 8) & 0xff) / 255,
                  blue: Double(rgb & 0xff) / 255,
                  opacity: alpha)
    }
}

enum AppTheme: String, CaseIterable, Codable, Identifiable {
    case light, dark, system
    var id: String { rawValue }
    var label: String { rawValue.capitalized }
}

struct AccentOption: Identifiable, Hashable {
    let name: String
    let hex: UInt
    var id: UInt { hex }
    var color: Color { Color(rgb: hex) }
}

enum Accents {
    static let all: [AccentOption] = [
        .init(name: "Blue",     hex: 0x0A84FF),
        .init(name: "Purple",   hex: 0xBF5AF2),
        .init(name: "Pink",     hex: 0xFF375F),
        .init(name: "Orange",   hex: 0xFF9F0A),
        .init(name: "Green",    hex: 0x30D158),
        .init(name: "Teal",     hex: 0x40C8E0),
        .init(name: "Graphite", hex: 0x98989D),
    ]
}

/// Resolved colors for the current theme + accent.
struct Palette {
    let isDark: Bool
    let accent: Color

    var stageTop: Color { isDark ? Color(rgb: 0x3a3a45) : Color(rgb: 0xdfe3ea) }
    var stageBottom: Color { isDark ? Color(rgb: 0x101013) : Color(rgb: 0xb7bcc7) }
    var bg: Color { isDark ? Color(rgb: 0x1f1f22) : Color(rgb: 0xececee) }
    var bar: Color { isDark ? Color(rgb: 0x2a2a2d) : Color(rgb: 0xf7f7f9) }
    var panel: Color { isDark ? Color(rgb: 0x2c2c2e) : Color(rgb: 0xffffff) }
    var tile: Color { isDark ? Color(rgb: 0x2c2c2e) : Color(rgb: 0xececed) }
    var rowSel: Color { isDark ? Color.white.opacity(0.07) : Color.black.opacity(0.055) }
    var rowHover: Color { isDark ? Color.white.opacity(0.05) : Color.black.opacity(0.035) }
    var text: Color { isDark ? Color(rgb: 0xffffff) : Color(rgb: 0x1d1d1f) }
    var text2: Color { isDark ? Color(rgb: 0xEBEBF5, 0.6) : Color(rgb: 0x3C3C43, 0.6) }
    var text3: Color { isDark ? Color(rgb: 0xEBEBF5, 0.32) : Color(rgb: 0x3C3C43, 0.3) }
    var hair: Color { isDark ? Color.white.opacity(0.09) : Color.black.opacity(0.09) }
    var field: Color { Color(rgb: 0x787880, isDark ? 0.26 : 0.16) }
    var track: Color { Color(rgb: 0x787880, isDark ? 0.34 : 0.26) }
    var mouseShell: Color { isDark ? Color(rgb: 0x3b3b41) : Color(rgb: 0xf4f4f6) }
    var mouseEdge: Color { isDark ? Color.white.opacity(0.18) : Color.black.opacity(0.16) }

    static func resolve(theme: AppTheme, systemScheme: ColorScheme, accent: Color) -> Palette {
        let isDark: Bool
        switch theme {
        case .dark:   isDark = true
        case .light:  isDark = false
        case .system: isDark = systemScheme == .dark
        }
        return Palette(isDark: isDark, accent: accent)
    }
}

private struct PaletteKey: EnvironmentKey {
    static let defaultValue = Palette(isDark: true, accent: Color(rgb: 0x0A84FF))
}

extension EnvironmentValues {
    var palette: Palette {
        get { self[PaletteKey.self] }
        set { self[PaletteKey.self] = newValue }
    }
}

// MARK: - Glossy surfaces

/// Frosted card: material, a top-lit gloss, a hairline rim that catches light,
/// and a soft drop shadow. Everything in the window sits on one of these.
struct Glass: ViewModifier {
    let pal: Palette
    var radius: CGFloat = 18
    var tint: Color? = nil
    func body(content: Content) -> some View {
        content
            .background(
                ZStack {
                    RoundedRectangle(cornerRadius: radius, style: .continuous).fill(.ultraThinMaterial)
                    RoundedRectangle(cornerRadius: radius, style: .continuous)
                        .fill(LinearGradient(colors: [Color.white.opacity(pal.isDark ? 0.10 : 0.60),
                                                      Color.white.opacity(pal.isDark ? 0.02 : 0.18)],
                                             startPoint: .top, endPoint: .bottom))
                    if let tint {
                        RoundedRectangle(cornerRadius: radius, style: .continuous).fill(tint.opacity(pal.isDark ? 0.10 : 0.07))
                    }
                }
            )
            .overlay(
                RoundedRectangle(cornerRadius: radius, style: .continuous)
                    .strokeBorder(LinearGradient(colors: [Color.white.opacity(pal.isDark ? 0.38 : 0.95),
                                                          Color.white.opacity(pal.isDark ? 0.06 : 0.35)],
                                                 startPoint: .topLeading, endPoint: .bottomTrailing), lineWidth: 1)
                    .allowsHitTesting(false)
            )
            .shadow(color: .black.opacity(pal.isDark ? 0.38 : 0.10), radius: 18, y: 9)
    }
}

extension View {
    func glass(_ pal: Palette, radius: CGFloat = 18, tint: Color? = nil) -> some View {
        modifier(Glass(pal: pal, radius: radius, tint: tint))
    }
}

/// The window backdrop: a vertical gradient with two soft colour glows.
struct Backdrop: View {
    let pal: Palette
    var body: some View {
        ZStack {
            LinearGradient(colors: [pal.stageTop, pal.bg, pal.stageBottom], startPoint: .top, endPoint: .bottom)
            Circle().fill(pal.accent.opacity(pal.isDark ? 0.30 : 0.26)).frame(width: 460, height: 460).blur(radius: 110).offset(x: -300, y: -260)
            Circle().fill(Color(rgb: 0xBF5AF2).opacity(pal.isDark ? 0.20 : 0.16)).frame(width: 420, height: 420).blur(radius: 110).offset(x: 330, y: 300)
            Circle().fill(Color(rgb: 0x40C8E0).opacity(pal.isDark ? 0.12 : 0.10)).frame(width: 300, height: 300).blur(radius: 90).offset(x: 260, y: -220)
        }
        .ignoresSafeArea()
    }
}

/// Capsule tab strip with a sliding accent pill.
struct PillTabs<T: Hashable & Identifiable & CaseIterable>: View where T.AllCases: RandomAccessCollection {
    @Binding var selection: T
    let title: (T) -> String
    let pal: Palette
    @Namespace private var ns

    var body: some View {
        HStack(spacing: 2) {
            ForEach(Array(T.allCases)) { t in
                let on = t == selection
                Button {
                    withAnimation(.spring(response: 0.32, dampingFraction: 0.82)) { selection = t }
                } label: {
                    Text(title(t))
                        .font(.system(size: 12, weight: on ? .semibold : .medium))
                        .foregroundStyle(on ? Color.white : pal.text2)
                        .padding(.horizontal, 14).padding(.vertical, 7)
                        .background {
                            if on {
                                Capsule()
                                    .fill(LinearGradient(colors: [pal.accent, pal.accent.opacity(0.78)], startPoint: .top, endPoint: .bottom))
                                    .overlay(Capsule().strokeBorder(Color.white.opacity(0.35), lineWidth: 0.8))
                                    .shadow(color: pal.accent.opacity(0.45), radius: 8, y: 3)
                                    .matchedGeometryEffect(id: "pill", in: ns)
                            }
                        }
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(4)
        .background(Capsule().fill(pal.field))
        .overlay(Capsule().strokeBorder(pal.hair).allowsHitTesting(false))
    }
}

/// Gradient wordmark.
struct Wordmark: View {
    let pal: Palette
    var size: CGFloat = 16
    var body: some View {
        Text("iXpat")
            .font(.system(size: size, weight: .bold, design: .rounded))
            .foregroundStyle(LinearGradient(colors: [pal.text, pal.accent], startPoint: .leading, endPoint: .trailing))
    }
}

/// Section heading with a small accent tick.
struct SectionTitle: View {
    let text: String
    let pal: Palette
    var body: some View {
        HStack(spacing: 8) {
            RoundedRectangle(cornerRadius: 2).fill(pal.accent).frame(width: 3, height: 14)
            Text(text).font(.system(size: 13, weight: .semibold)).foregroundStyle(pal.text)
        }
    }
}

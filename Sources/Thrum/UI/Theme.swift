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

/// Resolved colors for the current theme + accent. Same palette as Knurl so the
/// two apps look like siblings on the same Mac.
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

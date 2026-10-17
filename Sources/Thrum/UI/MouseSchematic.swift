import SwiftUI

/// Top-down outline of the Surface Mouse 2nd Edition: a tall pebble, slightly
/// narrower at the front, big-radius corners, one-piece top shell (no seam).
/// Proportions from Microsoft's spec: 4.3 in long, 2.48 in wide.
struct MouseBodyShape: Shape {
    func path(in r: CGRect) -> Path {
        let w = r.width, h = r.height
        func pt(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: r.minX + w * x, y: r.minY + h * y) }
        var p = Path()
        p.move(to: pt(0.5, 0))
        p.addCurve(to: pt(0.97, 0.22), control1: pt(0.80, 0), control2: pt(0.97, 0.06))      // front-right corner
        p.addCurve(to: pt(1.0, 0.70), control1: pt(0.99, 0.40), control2: pt(1.0, 0.55))      // right flank, flares out
        p.addCurve(to: pt(0.5, 1.0), control1: pt(1.0, 0.92), control2: pt(0.78, 1.0))        // rear-right corner
        p.addCurve(to: pt(0.0, 0.70), control1: pt(0.22, 1.0), control2: pt(0.0, 0.92))       // rear-left corner
        p.addCurve(to: pt(0.03, 0.22), control1: pt(0.0, 0.55), control2: pt(0.01, 0.40))     // left flank
        p.addCurve(to: pt(0.5, 0), control1: pt(0.03, 0.06), control2: pt(0.20, 0))           // front-left corner
        p.closeSubpath()
        return p
    }
}

/// Live schematic: buttons light up from the mouse's own reports, the wheel
/// turns with real notches, the action button glows while held, and every
/// haptic pulse sends a ring out from the body.
struct MouseSchematic: View {
    @EnvironmentObject var device: MouseDevice
    @EnvironmentObject var engine: HapticEngine
    @EnvironmentObject var button: ActionButtonController
    @Environment(\.palette) private var pal
    @State private var rings: [UUID] = []

    private let seamY: CGFloat = 0.48   // where the button zone ends (no visible seam on the real shell)

    var body: some View {
        GeometryReader { geo in
            let w = min(geo.size.width * 0.56, geo.size.height / 1.72)
            let h = w * 1.72
            let dx = max(-6, min(6, device.lastMotion.width)) * 0.6
            let dy = max(-6, min(6, device.lastMotion.height)) * 0.6
            ZStack {
                ForEach(rings, id: \.self) { _ in
                    PulseRing(color: pal.accent).frame(width: w * 0.9, height: w * 0.9)
                        .offset(y: h * 0.1)
                }
                ZStack {
                    MouseBodyShape()
                        .fill(LinearGradient(colors: [pal.mouseShell, pal.mouseShell.opacity(0.85)], startPoint: .top, endPoint: .bottom))
                        .shadow(color: .black.opacity(pal.isDark ? 0.5 : 0.22), radius: 14, y: 8)
                    // Left / right button fills
                    MouseBodyShape().fill(pal.accent.opacity(0.55))
                        .mask(Rectangle().frame(width: w / 2, height: h * seamY).offset(x: -w / 4, y: -h * (0.5 - seamY / 2)))
                        .opacity(device.buttons & 0x01 != 0 ? 1 : 0)
                    MouseBodyShape().fill(pal.accent.opacity(0.55))
                        .mask(Rectangle().frame(width: w / 2, height: h * seamY).offset(x: w / 4, y: -h * (0.5 - seamY / 2)))
                        .opacity(device.buttons & 0x02 != 0 ? 1 : 0)
                    MouseBodyShape().stroke(pal.mouseEdge, lineWidth: 1.5)
                    // Action button: small vertical pill at the very front
                    Capsule()
                        .fill(button.pressed ? pal.accent : (pal.isDark ? Color(rgb: 0x1b1b1f) : Color(rgb: 0xcfd2d8)))
                        .overlay(Capsule().strokeBorder(pal.isDark ? Color.white.opacity(0.35) : Color.black.opacity(0.25), lineWidth: 1))
                        .frame(width: w * 0.055, height: h * 0.085)
                        .position(x: w / 2, y: h * 0.115)
                        .shadow(color: pal.accent.opacity(button.pressed ? 0.8 : 0), radius: 8)
                    // Wheel, below the action button
                    WheelView(notches: device.wheelTotal, pressed: device.buttons & 0x04 != 0)
                        .frame(width: w * 0.10, height: h * 0.13)
                        .position(x: w / 2, y: h * 0.285)
                }
                .frame(width: w, height: h)
                .offset(x: dx, y: dy)
                .animation(.spring(duration: 0.22), value: device.lastMotion)
                .animation(.easeOut(duration: 0.08), value: device.buttons)
                .animation(.easeOut(duration: 0.1), value: button.pressed)
                .opacity(device.connected ? 1 : 0.45)
            }
            .frame(width: geo.size.width, height: geo.size.height)
        }
        .onChange(of: engine.pulseCount) { _, _ in
            let id = UUID()
            rings.append(id)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.7) { rings.removeAll { $0 == id } }
        }
    }
}

private struct WheelView: View {
    let notches: Int
    let pressed: Bool
    @Environment(\.palette) private var pal
    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 6).fill(pressed ? pal.accent : (pal.isDark ? Color(rgb: 0x1b1b1f) : Color(rgb: 0xcfd2d8)))
            Canvas { ctx, size in
                let step = size.height / 5
                let phase = CGFloat(((notches % 5) + 5) % 5) / 5 * step
                for i in -1...5 {
                    let y = CGFloat(i) * step + phase
                    guard y > 2, y < size.height - 2 else { continue }
                    var p = Path()
                    p.move(to: CGPoint(x: 3, y: y)); p.addLine(to: CGPoint(x: size.width - 3, y: y))
                    ctx.stroke(p, with: .color(pressed ? .white.opacity(0.7) : pal.text3), lineWidth: 1.2)
                }
            }
            RoundedRectangle(cornerRadius: 6).strokeBorder(pal.mouseEdge, lineWidth: 1)
        }
        .animation(.linear(duration: 0.06), value: notches)
    }
}

private struct PulseRing: View {
    let color: Color
    @State private var on = false
    var body: some View {
        Circle().strokeBorder(color, lineWidth: 2.5)
            .scaleEffect(on ? 2.1 : 0.5)
            .opacity(on ? 0 : 0.85)
            .onAppear { withAnimation(.easeOut(duration: 0.6)) { on = true } }
    }
}

/// Battery glyph + percentage.
struct BatteryBadge: View {
    let percent: Int?
    @Environment(\.palette) private var pal
    var body: some View {
        HStack(spacing: 5) {
            Image(systemName: symbol).foregroundStyle(color)
            Text(percent.map { "\($0)%" } ?? "—").font(.system(size: 11, weight: .medium)).foregroundStyle(pal.text2)
        }
    }
    private var symbol: String {
        guard let p = percent else { return "battery.0percent" }
        switch p { case 90...: return "battery.100percent"; case 60...: return "battery.75percent"
                   case 35...: return "battery.50percent"; case 15...: return "battery.25percent"; default: return "battery.0percent" }
    }
    private var color: Color { (percent ?? 100) < 15 ? .red : pal.text2 }
}

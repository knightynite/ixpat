import AppKit
import SwiftUI
import Combine

/// The Action Ring: a floating ring of chambers that opens under the pointer.
/// Sweep the mouse toward a chamber (haptic detent on each crossing), then
/// release the action button (hold mode) or click (toggle mode) to run it.
///
/// The panel ignores mouse events; the pointer is tracked by polling, so no
/// click ever lands on the ring itself, and the pointer stays wherever the
/// sweep left it.

final class RadialMenuController: ObservableObject {
    static let shared = RadialMenuController()

    @Published private(set) var items: [RingItem] = []
    @Published private(set) var selected: Int?
    @Published private(set) var needle: CGPoint = .zero      // unit vector, SwiftUI coords (y down)

    var onDetent: (() -> Void)?
    var onOpen: (() -> Void)?
    var onCommit: ((RingItem) -> Void)?

    /// Read from the tap thread (click / right-click interception) as well as main.
    var isOpen: Bool { _open.value }
    private let _open = Locked(false)
    private(set) var holdMode = false
    /// Accent colour for the wheel, pushed from Settings on main.
    var accentHex: UInt = 0x0A84FF
    private var panel: NSPanel?
    private var hosting: NSHostingView<RadialMenuView>?
    private var origin = CGPoint.zero          // AppKit screen coords (y up)
    private var poll: Timer?
    private var escapeMonitor: Any?

    static let size: CGFloat = 360
    static let deadZone: CGFloat = 26

    func toggle(items: [RingItem], holdMode: Bool) {
        if isOpen { close(commit: true) } else { open(items: items, holdMode: holdMode) }
    }

    func open(items: [RingItem], holdMode: Bool) {
        guard !isOpen, !items.isEmpty else { return }
        self.items = items
        self.holdMode = holdMode
        selected = nil
        needle = .zero
        origin = NSEvent.mouseLocation
        _open.value = true
        showPanel(at: origin)
        onOpen?()
        poll = Timer.scheduledTimer(withTimeInterval: 1.0 / 90, repeats: true) { [weak self] _ in
            self?.track()
        }
        escapeMonitor = NSEvent.addGlobalMonitorForEvents(matching: .keyDown) { [weak self] ev in
            if ev.keyCode == 53 { self?.close(commit: false) }
        }
    }

    /// Hold mode: the action button was released.
    func holdEnded() { if isOpen, holdMode { close(commit: true) } }

    func close(commit: Bool) {
        guard isOpen else { return }
        _open.value = false
        poll?.invalidate(); poll = nil
        if let escapeMonitor { NSEvent.removeMonitor(escapeMonitor); self.escapeMonitor = nil }
        let chosen = selected.flatMap { items.indices.contains($0) ? items[$0] : nil }
        hidePanel()
        if commit, let chosen { onCommit?(chosen) }
        selected = nil
    }

    private func track() {
        let p = NSEvent.mouseLocation
        let dx = p.x - origin.x, dy = -(p.y - origin.y)       // flip to y-down
        let dist = hypot(dx, dy)
        guard dist > Self.deadZone else {
            if selected != nil { selected = nil }
            needle = .zero
            return
        }
        needle = CGPoint(x: dx / dist, y: dy / dist)
        let idx = Self.slotIndex(angle: atan2(dy, dx), count: items.count)
        if idx != selected { selected = idx; onDetent?() }
    }

    /// Chamber 0 sits at 12 o'clock; chambers go clockwise.
    nonisolated static func slotIndex(angle: Double, count: Int) -> Int {
        var a = angle + .pi / 2                      // 0 at top
        if a < 0 { a += 2 * .pi }
        let slice = 2 * .pi / Double(count)
        return Int(((a + slice / 2) / slice).rounded(.down)) % count
    }

    private func showPanel(at center: CGPoint) {
        let s = Self.size
        let frame = NSRect(x: center.x - s / 2, y: center.y - s / 2, width: s, height: s)
        let panel = RadialPanel(contentRect: frame, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        let view = NSHostingView(rootView: RadialMenuView(controller: self))
        view.frame = NSRect(origin: .zero, size: frame.size)
        panel.contentView = view
        panel.orderFrontRegardless()
        self.panel = panel
        self.hosting = view
    }

    private func hidePanel() {
        panel?.orderOut(nil)
        panel = nil
        hosting = nil
    }
}

private final class RadialPanel: NSPanel {
    override init(contentRect: NSRect, styleMask: NSWindow.StyleMask, backing: NSWindow.BackingStoreType, defer flag: Bool) {
        super.init(contentRect: contentRect, styleMask: styleMask, backing: backing, defer: flag)
        level = .popUpMenu
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary, .ignoresCycle]
        isOpaque = false
        backgroundColor = .clear
        hasShadow = false
        ignoresMouseEvents = true
        hidesOnDeactivate = false
    }
}

/// One annular segment of the wheel. Chamber 0 is centred at 12 o'clock.
struct RingSegment: Shape {
    let index: Int, count: Int
    let inner: CGFloat, outer: CGFloat
    func path(in rect: CGRect) -> Path {
        let c = CGPoint(x: rect.midX, y: rect.midY)
        let slice = 2 * Double.pi / Double(count)
        let a0 = Angle.radians(Double(index) * slice - .pi / 2 - slice / 2)
        let a1 = Angle.radians(Double(index) * slice - .pi / 2 + slice / 2)
        var p = Path()
        p.addArc(center: c, radius: outer, startAngle: a0, endAngle: a1, clockwise: false)
        p.addArc(center: c, radius: inner, startAngle: a1, endAngle: a0, clockwise: true)
        p.closeSubpath()
        return p
    }
}

/// MX-style Actions Ring: a dark wheel of wedge chambers around a hub that
/// names the highlighted action; the chosen wedge lights up in the accent.
struct RadialMenuView: View {
    @ObservedObject var controller: RadialMenuController
    @State private var shown = false
    private var accent: Color { Color(rgb: controller.accentHex) }

    var body: some View {
        let s = RadialMenuController.size
        let center = CGPoint(x: s / 2, y: s / 2)
        let n = max(controller.items.count, 1)
        let outer = s * 0.46, inner = s * 0.24
        let mid = (outer + inner) / 2
        ZStack {
            // Disc
            Circle().fill(Color(rgb: 0x151518).opacity(0.86))
                .frame(width: outer * 2 + 6, height: outer * 2 + 6)
                .shadow(color: .black.opacity(0.5), radius: 18, y: 6)
            // Wedges
            ForEach(Array(controller.items.enumerated()), id: \.element.id) { i, item in
                let on = controller.selected == i
                RingSegment(index: i, count: n, inner: inner, outer: outer)
                    .fill(on ? accent : Color.white.opacity(0.06))
                    .overlay(RingSegment(index: i, count: n, inner: inner, outer: outer)
                                .stroke(Color(rgb: 0x151518), lineWidth: 3))
                    .shadow(color: on ? accent.opacity(0.6) : .clear, radius: 10)
                    .animation(.easeOut(duration: 0.12), value: controller.selected)
            }
            // Icons
            ForEach(Array(controller.items.enumerated()), id: \.element.id) { i, item in
                let a = Double(i) / Double(n) * 2 * .pi - .pi / 2
                let on = controller.selected == i
                Image(systemName: item.symbol.isEmpty ? "questionmark" : item.symbol)
                    .font(.system(size: on ? 22 : 19, weight: .medium))
                    .foregroundStyle(on ? .white : .white.opacity(0.8))
                    .position(x: center.x + CGFloat(cos(a)) * mid, y: center.y + CGFloat(sin(a)) * mid)
                    .animation(.easeOut(duration: 0.12), value: controller.selected)
            }
            // Hub
            VStack(spacing: 2) {
                Text(controller.selected.map { controller.items[$0].title } ?? "Actions")
                    .font(.system(size: 13, weight: .semibold)).foregroundStyle(.white)
                    .lineLimit(1).minimumScaleFactor(0.7)
                Text(controller.holdMode ? "release to run" : "click to run")
                    .font(.system(size: 9)).foregroundStyle(.white.opacity(0.5))
            }
            .frame(width: inner * 2 - 10, height: inner * 2 - 10)
            .background(Circle().fill(Color(rgb: 0x1f1f24)))
            .overlay(Circle().strokeBorder(Color.white.opacity(0.12)))
        }
        .frame(width: s, height: s)
        .scaleEffect(shown ? 1 : 0.7)
        .opacity(shown ? 1 : 0)
        .onAppear { withAnimation(.spring(duration: 0.2, bounce: 0.2)) { shown = true } }
    }
}

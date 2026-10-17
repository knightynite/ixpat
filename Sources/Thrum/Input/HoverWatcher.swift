import AppKit
import QuartzCore

/// Hover affordance ticks from the system cursor shape. `NSCursor.currentSystem`
/// is readable from any process and changes when the pointer enters a window
/// edge (resize), text (I-beam), a link (hand) or a drop target (badged drag
/// cursor). That is the same signal Windows uses for its Close-button hover.
///
/// Main thread: the cursor is an AppKit object. Polled only while the pointer
/// has moved recently, so an idle Mac costs nothing.
final class HoverWatcher {
    struct Config { var hoverTicks = false; var dropTicks = false }
    var config = Config()
    var pointerMovedRecently: () -> Bool = { false }
    var leftButtonDown: () -> Bool = { false }
    var onHover: (() -> Void)?
    var onDropTarget: (() -> Void)?

    private var timer: Timer?
    private var lastKey = ""
    private var lastTick: CFTimeInterval = 0
    private lazy var arrowKey = Self.key(NSCursor.arrow)

    func start() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in self?.tick() }
        timer?.tolerance = 0.01
    }

    func stop() { timer?.invalidate(); timer = nil }

    private static func key(_ c: NSCursor) -> String {
        let s = c.image.size, h = c.hotSpot
        return "\(Int(s.width))x\(Int(s.height))@\(Int(h.x)),\(Int(h.y))"
    }

    private func tick() {
        let c = config
        guard c.hoverTicks || c.dropTicks, pointerMovedRecently(), let cur = NSCursor.currentSystem else { return }
        let k = Self.key(cur)
        guard k != lastKey else { return }
        let wasArrow = lastKey == arrowKey || lastKey.isEmpty
        lastKey = k
        let now = CACurrentMediaTime()
        guard now - lastTick >= 0.08 else { return }
        if leftButtonDown() {
            // A drag: the cursor gains or loses a badge as it crosses a valid target.
            guard c.dropTicks else { return }
            lastTick = now; onDropTarget?()
        } else {
            // Entering an affordance (arrow → something else). Leaving is silent, as on Windows.
            guard c.hoverTicks, k != arrowKey, wasArrow || true else { return }
            lastTick = now; onHover?()
        }
    }
}

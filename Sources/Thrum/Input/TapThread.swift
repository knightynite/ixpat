import Foundation

/// A dedicated thread with its own run loop for the CGEvent tap, so nothing
/// the app does on the main thread (SwiftUI, accessibility calls, timers)
/// can delay the system's pointer events.
final class TapThread {
    static let shared = TapThread()
    private(set) var runLoop: CFRunLoop!
    private let ready = DispatchSemaphore(value: 0)
    private var thread: Thread?

    private init() {
        let t = Thread { [self] in
            runLoop = CFRunLoopGetCurrent()
            var ctx = CFRunLoopSourceContext()
            let keepAlive = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx)   // keeps the loop alive
            CFRunLoopAddSource(runLoop, keepAlive, .commonModes)
            ready.signal()
            CFRunLoopRun()
        }
        t.name = "com.alcybr.thrum.eventtap"
        t.qualityOfService = .userInteractive
        t.start()
        thread = t
        ready.wait()
    }
}

/// A value shared between threads (main, the tap thread, the AX queue) behind a
/// lock. Used for the handful of flags and configs those threads exchange, so no
/// `@Published` property or plain stored var is ever read off the thread that
/// writes it.
final class Locked<T>: @unchecked Sendable {
    private let lock = NSLock()
    private var v: T
    init(_ v: T) { self.v = v }
    var value: T {
        get { lock.lock(); defer { lock.unlock() }; return v }
        set { lock.lock(); v = newValue; lock.unlock() }
    }
    func mutate(_ f: (inout T) -> Void) { lock.lock(); f(&v); lock.unlock() }
}

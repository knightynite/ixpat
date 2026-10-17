import AppKit
import CoreAudio
import AudioToolbox
import QuartzCore

/// Hold-and-move gestures: while the action button is held (the firmware's hold
/// chord is down) the pointer is frozen and its motion drives actions instead,
/// MX "gesture button" style. Vertical motion steps (volume, brightness) every
/// `stepPx`; horizontal motion swipes (track change, desktops) every `swipePx`.
/// The first 10 px decide the axis, which then stays locked for the hold.
///
/// Threads: `begin`/`end` on main, `handleMove` on the tap thread. Actions are
/// handed back to main through `onAction`.
final class GestureController {
    struct Config {
        var up: ButtonAction = .volumeUp, down: ButtonAction = .volumeDown
        var left: ButtonAction = .previousTrack, right: ButtonAction = .nextTrack
        var stepPx = 24.0, swipePx = 90.0
        var freezePointer = true
    }
    var config: Config { get { _config.value } set { _config.value = newValue } }
    private let _config = Locked(Config())
    private let _active = Locked(false)
    var isActive: Bool { _active.value }
    /// Called on main for every step or swipe.
    var onAction: ((ButtonAction) -> Void)?
    /// Actions fired during the current / last hold (a hold with none is a plain hold).
    var firedCount: Int { _fired.value }
    private let _fired = Locked(0)

    // Tap-thread state
    private var accX = 0.0, accY = 0.0
    private var axis = 0            // 0 undecided, 1 horizontal, 2 vertical
    private var frozen = false

    func begin() {
        guard !_active.value else { return }
        accX = 0; accY = 0; axis = 0
        _fired.value = 0
        _active.value = true
        if config.freezePointer { CGAssociateMouseAndMouseCursorPosition(0); frozen = true }
        DebugLog.log("gesture mode begin")
    }

    func end() {
        guard _active.value else { return }
        _active.value = false
        if frozen { CGAssociateMouseAndMouseCursorPosition(1); frozen = false }
        DebugLog.log("gesture mode end axis=\(axis)")
    }

    /// Tap thread. Returns true when the motion was consumed by a gesture.
    func handleMove(dx: Double, dy: Double) -> Bool {
        guard _active.value else { return false }
        let c = config
        accX += dx; accY += dy
        if axis == 0 {
            guard hypot(accX, accY) >= 10 else { return true }
            axis = abs(accX) >= abs(accY) ? 1 : 2
            accX = 0; accY = 0
            return true
        }
        let acc = axis == 1 ? accX : accY
        let action: ButtonAction = axis == 1 ? (acc < 0 ? c.left : c.right) : (acc < 0 ? c.up : c.down)
        let unit = action.isStepped ? c.stepPx : c.swipePx
        guard unit > 0 else { return true }
        var fired = 0
        while abs(axis == 1 ? accX : accY) >= unit, fired < 6 {
            if axis == 1 { accX -= accX < 0 ? -unit : unit } else { accY -= accY < 0 ? -unit : unit }
            fired += 1
            _fired.mutate { $0 += 1 }
            DispatchQueue.main.async { [weak self] in self?.onAction?(action) }
        }
        return true
    }
}

/// Output volume of the default device, 0…1, for the "already at the limit" bump.
enum SystemVolume {
    static func current() -> Float? {
        var dev = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultOutputDevice,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &dev) == noErr, dev != 0 else { return nil }
        var vol: Float32 = 0
        var vsize = UInt32(MemoryLayout<Float32>.size)
        var vaddr = AudioObjectPropertyAddress(mSelector: kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
                                               mScope: kAudioObjectPropertyScopeOutput,
                                               mElement: kAudioObjectPropertyElementMain)
        guard AudioObjectGetPropertyData(dev, &vaddr, 0, nil, &vsize, &vol) == noErr else { return nil }
        return vol
    }
}

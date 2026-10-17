import Foundation
import Combine
import QuartzCore

/// Plays device waveforms with rate limiting, and tells the UI when it did.
final class HapticEngine: ObservableObject {
    @Published private(set) var pulseCount = 0
    @Published private(set) var lastPlayed = "—"
    @Published private(set) var lastEvent = "—"

    weak var device: MouseDevice?
    weak var settings: Settings?

    private var lastAny: CFTimeInterval = 0

    /// Fire one waveform now (used by the test buttons and the rule engine).
    func play(ordinal: Int, intensity: Int, label: String) {
        guard let device, device.connected else { return }
        let now = CACurrentMediaTime()
        let dur = device.waveforms.first { $0.ordinal == ordinal }?.durationMs ?? 50
        // The device plays one waveform at a time; don't pile triggers up.
        if now - lastAny < max(Double(dur) / 1000, 0.035) { return }
        lastAny = now
        device.trigger(ordinal: ordinal, intensity: intensity)
        pulseCount += 1
        lastPlayed = "\(label) · \(device.waveforms.first { $0.ordinal == ordinal }?.name ?? "#\(ordinal)") · \(intensity)"
    }

    /// Fire the waveform configured for an event, honouring master switches.
    /// `ordinal` swaps the rule's waveform for this one play; `boost` shifts the rule's
    /// intensity (±) before master scaling, for velocity-dependent feels.
    func play(_ event: HapticEvent, ordinal: Int? = nil, boost: Int = 0) {
        guard let settings, settings.enabled, settings.hapticsEnabled else { return }
        let rule = settings.rule(event)
        guard rule.enabled else { return }
        lastEvent = event.title
        // Master strength scales the rule's own (both 0…4). A low master setting must
        // soften light rules, not silence them: anything enabled plays at least level 1.
        guard settings.masterIntensity > 0, rule.intensity > 0 else { return }
        let base = max(1, min(4, rule.intensity + boost))
        let intensity = max(1, Int((Double(base) * Double(settings.masterIntensity) / 4.0).rounded()))
        play(ordinal: ordinal ?? rule.ordinal, intensity: intensity, label: event.title)
        if let e = rule.echo, e.intensity > 0 {
            let ei = max(1, Int((Double(max(1, min(4, e.intensity + boost))) * Double(settings.masterIntensity) / 4.0).rounded()))
            DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(max(20, e.delayMs))) { [weak self] in
                self?.play(ordinal: e.ordinal, intensity: ei, label: event.title + " echo")
            }
        }
    }

    /// Preview a rule as the user would feel it (pulse + echo), for the test buttons.
    func preview(_ r: HapticRule, label: String) {
        play(ordinal: r.ordinal, intensity: r.intensity, label: label)
        if let e = r.echo { DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(max(20, e.delayMs))) { [weak self] in self?.play(ordinal: e.ordinal, intensity: e.intensity, label: label + " echo") } }
    }
}

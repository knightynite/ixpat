#include "core/router.h"

#include <cctype>

namespace ixpat {
namespace {
constexpr int kFreeSpinRestMs = 300;
}

Router::Router(Settings& settings, RouterSinks sinks) : s_(settings), k_(std::move(sinks)) {}

void Router::setTable(std::span<const Waveform> table) { table_.assign(table.begin(), table.end()); }

int Router::ordinalOr(std::uint16_t usage, int fallback) const { return ordinalFor(usage, table_).value_or(fallback); }

WheelFeelParams Router::wheelParams() const {
    WheelFeelParams p;
    p.enabled = s_.enabled;
    p.freeSpin = s_.effFreeSpin();
    p.ratchetDynamic = s_.effRatchetDynamic();
    p.freeSpinThreshold = s_.freeSpinThreshold;
    p.ratchetLightRate = s_.effRatchetLightRate();
    p.scrollStepEvery = s_.effScrollStepEvery();
    p.freeSpinTextureEvery = s_.effFreeSpinTextureEvery();
    p.scrollStepOrdinal = s_.rule(HapticEvent::scrollStep).ordinal;
    p.collideOrdinal = ordinalOr(0x1012, 4);
    p.stepOrdinal = ordinalOr(0x1014, 6);
    p.hoverOrdinal = ordinalOr(0x1008, 3);
    return p;
}

// MARK: buttons and hold timers

void Router::armHold(int id, HapticEvent holdEvent) {
    if (k_.cancelTimer) k_.cancelTimer(id);
    if (!s_.rule(holdEvent).enabled) return;
    if (k_.armTimer) k_.armTimer(id, s_.clickHoldMs);
}

void Router::buttons(std::uint8_t oldMask, std::uint8_t newMask, Seconds) {
    buttons_ = newMask;
    if (!s_.enabled) return;
    const std::uint8_t went = static_cast<std::uint8_t>(newMask & ~oldMask);
    const std::uint8_t left = static_cast<std::uint8_t>(oldMask & ~newMask);
    if (went & 0x01) {
        k_.play(HapticEvent::leftDown, std::nullopt, 0);
        armHold(kTimerLeftHold, HapticEvent::leftHold);
    }
    if (left & 0x01) {
        k_.play(HapticEvent::leftUp, std::nullopt, 0);
        if (k_.cancelTimer) k_.cancelTimer(kTimerLeftHold);
    }
    if (went & 0x02) {
        k_.play(HapticEvent::rightDown, std::nullopt, 0);
        armHold(kTimerRightHold, HapticEvent::rightHold);
    }
    if (left & 0x02) {
        k_.play(HapticEvent::rightUp, std::nullopt, 0);
        if (k_.cancelTimer) k_.cancelTimer(kTimerRightHold);
    }
    if (went & 0x04) k_.play(HapticEvent::middleDown, std::nullopt, 0);
}

void Router::timer(int id, Seconds now) {
    switch (id) {
        case kTimerLeftHold:
            if (buttons_ & 0x01) k_.play(HapticEvent::leftHold, std::nullopt, 0);
            break;
        case kTimerRightHold:
            if (buttons_ & 0x02) k_.play(HapticEvent::rightHold, std::nullopt, 0);
            break;
        case kTimerFreeSpinIdle:
            if (auto p = wheel_.idle(now, wheelParams())) k_.play(p->event, p->ordinal, p->boost);
            else if (wheel_.freeSpinActive() && k_.armTimer) k_.armTimer(kTimerFreeSpinIdle, kFreeSpinRestMs);
            break;
        default:
            break;
    }
}

// MARK: wheel

void Router::wheel(int notches, Seconds now) {
    for (const auto& p : wheel_.notch(notches, now, wheelParams())) k_.play(p.event, p.ordinal, p.boost);
    if (wheel_.freeSpinActive() && k_.armTimer) k_.armTimer(kTimerFreeSpinIdle, kFreeSpinRestMs);
}

// MARK: action button

void Router::chord(ChordOutput out, Seconds now) {
    switch (out) {
        case ChordOutput::press:
            onGesture(ButtonGesture::press, now);
            break;
        case ChordOutput::doublePress:
            onGesture(ButtonGesture::doublePress, now);
            break;
        case ChordOutput::holdBegin:
            chordHeld_ = true;
            onGesture(ButtonGesture::hold, now);
            break;
        case ChordOutput::holdEnd:
            chordHeld_ = false;
            holdEnded(now);
            break;
        default:
            break;
    }
}

void Router::onGesture(ButtonGesture g, Seconds) {
    if (!s_.enabled) return;
    k_.play(HapticEvent::actionButton, std::nullopt, 0);
    const ButtonAction& a = s_.action(g);
    if (a.kind == ActionKind::none) return;
    // A hold starts gesture mode (unless it drives the ring, which needs the pointer); its own action
    // waits for the release and runs only when the mouse stayed put.
    if (g == ButtonGesture::hold && s_.gesturesOnHold && a.kind != ActionKind::actionRing &&
        a.kind != ActionKind::gestures && chordHeld_) {
        pendingHold_ = a;
        gestureActive_ = true;
        gestureFired_ = 0;
        if (k_.gesturesBegin) k_.gesturesBegin();
        return;
    }
    run(a, g == ButtonGesture::hold);
}

void Router::run(const ButtonAction& a, bool hold) {
    switch (a.kind) {
        case ActionKind::actionRing:
            if (hold) {
                if (k_.ringOpen) k_.ringOpen(true);
            } else if (k_.ringToggle) {
                k_.ringToggle();
            }
            break;
        case ActionKind::gestures:
            // Only while the chord is really down: a test button must not freeze the pointer.
            if (hold && chordHeld_) {
                gestureActive_ = true;
                gestureFired_ = 0;
                if (k_.gesturesBegin) k_.gesturesBegin();
            }
            break;
        default:
            if (k_.runAction) k_.runAction(a, hold);
            break;
    }
}

void Router::holdEnded(Seconds) {
    if (k_.ringHoldEnded) k_.ringHoldEnded();
    const bool gestured = gestureFired_ > 0;
    if (gestureActive_ && k_.gesturesEnd) k_.gesturesEnd();
    gestureActive_ = false;
    if (pendingHold_) {
        ButtonAction a = *pendingHold_;
        pendingHold_.reset();
        if (!gestured) run(a, true);  // a hold without movement is still the hold action
    }
}

void Router::gestureFired(const ButtonAction& a, Seconds) {
    ++gestureFired_;
    switch (a.kind) {
        case ActionKind::volumeUp:
        case ActionKind::volumeDown: {
            // At the end of the range the system ignores the key; play a bump instead of a step.
            const auto v = k_.systemVolume ? k_.systemVolume() : std::nullopt;
            if (v && ((a.kind == ActionKind::volumeUp && *v >= 0.999f) || (a.kind == ActionKind::volumeDown && *v <= 0.001f))) {
                k_.play(HapticEvent::gestureLimit, std::nullopt, 0);
                return;
            }
            if (k_.runAction) k_.runAction(a, false);
            k_.play(HapticEvent::gestureStep, std::nullopt, 0);
            break;
        }
        case ActionKind::brightnessUp:
        case ActionKind::brightnessDown:
            if (k_.runAction) k_.runAction(a, false);
            k_.play(HapticEvent::gestureStep, std::nullopt, 0);
            break;
        case ActionKind::previousTrack:
            run(a, false);
            k_.play(HapticEvent::gestureSwipe, ordinalOr(0x100A, 9), 0);  // Error: the descending pattern
            break;
        default:
            run(a, false);
            k_.play(HapticEvent::gestureSwipe, std::nullopt, 0);
            break;
    }
}

// MARK: system

void Router::systemEvent(HapticEvent e, Seconds) { k_.play(e, std::nullopt, 0); }

void Router::foregroundChanged(std::string imageName, Seconds) {
    for (auto& c : imageName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    s_.frontBundleID = std::move(imageName);
    k_.play(HapticEvent::appSwitch, std::nullopt, 0);
}

void Router::deviceConnected(bool on, Seconds) {
    if (on) return;
    // A hold cannot end normally once the mouse is gone.
    if (gestureActive_ && k_.gesturesEnd) k_.gesturesEnd();
    gestureActive_ = false;
    pendingHold_.reset();
    chordHeld_ = false;
    if (k_.ringHoldEnded) k_.ringHoldEnded();
}

}  // namespace ixpat

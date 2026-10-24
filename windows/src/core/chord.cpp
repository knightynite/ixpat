#include "core/chord.h"

namespace ixpat {

void ChordDecoder::dropIfStale(Seconds now) {
    if (!stale(now)) return;
    // kbdhid lost a break (or the mouse went to sleep mid-chord): forget the keys silently; nothing fires.
    down_.reset();
    chord_ = Chord::none;
    clickSeen_ = false;
    consumed_ = false;
    holdByTime_ = false;
}

ChordOutput ChordDecoder::poll(Seconds now) {
    dropIfStale(now);
    if (queued_ != ChordOutput::none) {
        const ChordOutput q = queued_;
        queued_ = ChordOutput::none;
        return q;
    }
    if (decisionPending_ && now >= decideAt_) {
        decisionPending_ = false;  // nothing followed the release: Click to Do, as on the Mac
        return pendingConsumed_ ? ChordOutput::none : ChordOutput::doublePress;
    }
    if (chord_ == Chord::win && !clickSeen_ && !holdByTime_ && now - winSince_ >= kHoldAfter) {
        holdByTime_ = true;  // still down with no S and no click: the firmware is holding Win+Shift+F23
        chord_ = Chord::copilot;
        return consumed_ ? ChordOutput::none : ChordOutput::holdBegin;
    }
    return ChordOutput::none;
}

Seconds ChordDecoder::nextDeadline() const {
    Seconds d = 0;
    if (queued_ != ChordOutput::none) return 1e-9;
    if (decisionPending_) d = decideAt_;
    if (chord_ == Chord::win && !clickSeen_ && !holdByTime_) {
        const Seconds h = winSince_ + kHoldAfter;
        d = d > 0 ? (h < d ? h : d) : h;
    }
    return d;
}

ChordOutput ChordDecoder::key(int vk, bool down, Seconds now) {
    if (vk < 0 || vk >= 256) return ChordOutput::none;
    dropIfStale(now);
    deviceKeyUntil_ = now + kKeyWindow;
    if (down) {
        if (down_[vk]) return ChordOutput::none;  // key repeat
        down_[vk] = true;
        // Evaluate the whole key set on every make, like the Mac reads each report: kbdhid may emit the
        // modifiers and the key of one report in any order.
        if (down_[kVkS] && winDown() && shiftDown()) return chordDown(Chord::snip, now);
        if (down_[kVkF23] && winDown() && shiftDown()) return chordDown(Chord::copilot, now);
        if (winDown() && chord_ == Chord::none && !down_[kVkS] && !down_[kVkF23]) return chordDown(Chord::win, now);
        return ChordOutput::none;
    }
    down_[vk] = false;
    if (decisionPending_ && (vk == kVkS || vk == kVkF23)) {
        // The break of the letter whose make never reached us: that is what the chord was.
        decisionPending_ = false;
        const bool consumed = pendingConsumed_;
        swallowClicksUntil_ = 0;  // a press or a hold promises no fake click
        if (consumed) return ChordOutput::none;
        if (vk == kVkS) return ChordOutput::press;
        queued_ = ChordOutput::holdEnd;  // a hold shorter than kHoldAfter: begin now, end on the next poll
        return ChordOutput::holdBegin;
    }
    if (down_.none()) return chordUp(now);  // the chord ends when the last of its keys is up
    return ChordOutput::none;
}

ChordOutput ChordDecoder::mouseLeftDown(Seconds now) {
    dropIfStale(now);
    if (decisionPending_) {  // the fake click landed after the Win release: this is the double press
        decisionPending_ = false;
        swallowClicksUntil_ = now + kClickWindow;
        return pendingConsumed_ ? ChordOutput::none : ChordOutput::doublePress;
    }
    // The "double press" chord is Win + a left click the firmware fakes.
    if (chord_ != Chord::win || clickSeen_) return ChordOutput::none;
    clickSeen_ = true;
    swallowClicksUntil_ = now + kClickWindow;
    return consumed_ ? ChordOutput::none : ChordOutput::doublePress;
}

ChordOutput ChordDecoder::chordDown(Chord c, Seconds now) {
    if (chord_ == c) return ChordOutput::none;
    // A chord only upgrades (Win alone -> Win+Shift+S as the keys arrive one by one); it never falls
    // back to Win while its keys are being released in whatever order kbdhid emits them.
    if (chord_ != Chord::none && c == Chord::win) return ChordOutput::none;
    chord_ = c;
    clickSeen_ = false;
    if (c == Chord::win) {
        winSince_ = now;
        holdByTime_ = false;
        decisionPending_ = false;
    }
    if (consumed_) return ChordOutput::none;
    if (pressIntercept && pressIntercept()) {
        consumePress(now);
        return ChordOutput::none;
    }
    switch (c) {
        case Chord::snip:
            return ChordOutput::press;
        case Chord::copilot:
            return ChordOutput::holdBegin;  // the firmware already waited for the hold
        default:
            return ChordOutput::none;  // win: the fake click that follows confirms the double press
    }
}

ChordOutput ChordDecoder::chordUp(Seconds now) {
    const Chord c = chord_;
    const bool clickSeen = clickSeen_, consumed = consumed_;
    chord_ = Chord::none;
    clickSeen_ = false;
    consumed_ = false;
    holdByTime_ = false;
    switch (c) {
        case Chord::copilot:
            return consumed ? ChordOutput::none : ChordOutput::holdEnd;
        case Chord::win:
            if (!clickSeen) {
                // Nothing decided it yet: the S break (press), an F23 break (hold) or the fake click (double
                // press) follows within a few ms; failing all three, it is Click to Do once the grace ends.
                decisionPending_ = true;
                pendingConsumed_ = consumed;
                decideAt_ = now + kDecisionGrace;
                swallowClicksUntil_ = now + kClickWindow;
            }
            return ChordOutput::none;
        default:
            return ChordOutput::none;
    }
}

void ChordDecoder::consumePress(Seconds now) {
    consumed_ = true;
    // The Win chord's fake click still follows and must stay hidden.
    swallowClicksUntil_ = now + kClickWindow;
}

void ChordDecoder::reset() {
    down_.reset();
    chord_ = Chord::none;
    clickSeen_ = false;
    consumed_ = false;
    deviceKeyUntil_ = 0;
    swallowClicksUntil_ = 0;
    winSince_ = 0;
    holdByTime_ = false;
    decisionPending_ = false;
    pendingConsumed_ = false;
    decideAt_ = 0;
    queued_ = ChordOutput::none;
}

}  // namespace ixpat

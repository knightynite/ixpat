#include "core/chord.h"

namespace ixpat {

void ChordDecoder::dropIfStale(Seconds now) {
    if (!stale(now)) return;
    // kbdhid lost a break (or the mouse went to sleep mid-chord): forget the keys silently; nothing fires.
    down_.reset();
    chord_ = Chord::none;
    clickSeen_ = false;
    consumed_ = false;
    shiftInChord_ = false;
}

ChordOutput ChordDecoder::key(int vk, bool down, Seconds now) {
    if (vk < 0 || vk >= 256) return ChordOutput::none;
    dropIfStale(now);
    deviceKeyUntil_ = now + kKeyWindow;
    if (down) {
        if (down_[vk]) return ChordOutput::none;  // key repeat: keeps the chord fresh, changes nothing
        down_[vk] = true;
        if (chord_ == Chord::win && shiftDown()) shiftInChord_ = true;
        // Evaluate the whole key set on every make, like the Mac reads each report: kbdhid may emit the
        // modifiers and the key of one report in any order.
        if (down_[kVkS] && winDown() && shiftDown()) return chordDown(Chord::snip, now);
        if (down_[kVkF23] && winDown() && shiftDown()) return chordDown(Chord::copilot, now);
        if (winDown() && chord_ == Chord::none && !down_[kVkS] && !down_[kVkF23]) return chordDown(Chord::win, now);
        return ChordOutput::none;
    }
    down_[vk] = false;
    if (down_.none()) return chordUp(now);  // the chord ends when the last of its keys is up
    return ChordOutput::none;
}

ChordOutput ChordDecoder::mouseLeftDown(Seconds now) {
    dropIfStale(now);
    if (chord_ == Chord::win && !clickSeen_ && !shiftInChord_) {
        // The "double press" chord is Win + a left click the firmware fakes. Shift in the chord means a press or
        // a hold is on its way instead, and a click then is the user's own.
        clickSeen_ = true;
        swallowClicksUntil_ = now + kClickWindow;
        if (consumed_) return ChordOutput::none;
        if (pressIntercept && pressIntercept()) {
            consumed_ = true;
            return ChordOutput::none;
        }
        return ChordOutput::doublePress;
    }
    if (clickFires_ && now < swallowClicksUntil_) {
        // The Win chord was already released: this late click is the one it promised.
        clickFires_ = false;
        return ChordOutput::doublePress;
    }
    return ChordOutput::none;
}

ChordOutput ChordDecoder::chordDown(Chord c, Seconds now) {
    if (chord_ == c) return ChordOutput::none;
    // A chord only upgrades, from Win alone to Win+Shift+S or Win+Shift+F23 as the keys arrive one by one. Once
    // it is a press or a hold, no other key changes what it is, and it never falls back to Win while its keys
    // are being released in whatever order kbdhid emits them.
    if (chord_ != Chord::none && chord_ != Chord::win) return ChordOutput::none;
    if (chord_ == Chord::win && c == Chord::win) return ChordOutput::none;
    chord_ = c;
    clickSeen_ = false;
    clickFires_ = false;
    if (c == Chord::win) shiftInChord_ = shiftDown();
    if (consumed_) return ChordOutput::none;
    if (c == Chord::win) return ChordOutput::none;  // a candidate only: the letter or the fake click decides
    // A concrete gesture: the ring, if it is open in toggle mode, takes it as its commit instead.
    if (pressIntercept && pressIntercept()) {
        consumed_ = true;
        return ChordOutput::none;
    }
    (void)now;
    return c == Chord::snip ? ChordOutput::press : ChordOutput::holdBegin;  // the firmware already waited for the hold
}

ChordOutput ChordDecoder::chordUp(Seconds now) {
    const Chord c = chord_;
    const bool clickSeen = clickSeen_, consumed = consumed_, withShift = shiftInChord_;
    chord_ = Chord::none;
    clickSeen_ = false;
    consumed_ = false;
    shiftInChord_ = false;
    switch (c) {
        case Chord::copilot:
            return consumed ? ChordOutput::none : ChordOutput::holdEnd;
        case Chord::win:
            if (!clickSeen && !withShift) {
                // Win alone is the click chord, and the fake click can land after the Win release: hide it, and
                // let it fire. Win+Shift with no letter heard was a press or hold Windows already acted on
                // (the letter never reached the decoder): nothing fires, no click is hidden.
                clickFires_ = !consumed;
                swallowClicksUntil_ = now + kClickWindow;
            }
            return ChordOutput::none;
        default:
            return ChordOutput::none;
    }
}

void ChordDecoder::consumePress(Seconds now) {
    consumed_ = true;
    // Only the click chord has a fake click still to come; that one must stay hidden.
    if (chord_ == Chord::win && !shiftInChord_) swallowClicksUntil_ = now + kClickWindow;
}

void ChordDecoder::reset() {
    down_.reset();
    chord_ = Chord::none;
    clickSeen_ = false;
    consumed_ = false;
    shiftInChord_ = false;
    clickFires_ = false;
    deviceKeyUntil_ = 0;
    swallowClicksUntil_ = 0;
}

}  // namespace ixpat

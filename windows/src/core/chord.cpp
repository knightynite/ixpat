#include "core/chord.h"

namespace ixpat {

ChordOutput ChordDecoder::key(int vk, bool down, Seconds now) {
    if (vk < 0 || vk >= 256) return ChordOutput::none;
    deviceKeyUntil_ = now + kKeyWindow;
    if (down) {
        if (down_[vk]) return ChordOutput::none;  // key repeat
        down_[vk] = true;
        if (vk == kVkS && winDown() && shiftDown()) return chordDown(Chord::snip, now);
        if (vk == kVkF23 && winDown() && shiftDown()) return chordDown(Chord::copilot, now);
        if ((vk == kVkLWin || vk == kVkRWin) && chord_ == Chord::none) return chordDown(Chord::win, now);
        return ChordOutput::none;
    }
    down_[vk] = false;
    if (down_.none()) return chordUp();  // the chord ends when the last of its keys is up
    return ChordOutput::none;
}

ChordOutput ChordDecoder::mouseLeftDown(Seconds now) {
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

ChordOutput ChordDecoder::chordUp() {
    const Chord c = chord_;
    const bool clickSeen = clickSeen_, consumed = consumed_;
    chord_ = Chord::none;
    clickSeen_ = false;
    consumed_ = false;
    if (consumed) return ChordOutput::none;
    switch (c) {
        case Chord::copilot:
            return ChordOutput::holdEnd;
        case Chord::win:
            if (!clickSeen) {
                // The click never came (or comes late): still Click to Do; keep the late click hidden.
                swallowClicksUntil_ = deviceKeyUntil_ - kKeyWindow + kClickWindow;
                return ChordOutput::doublePress;
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
}

}  // namespace ixpat

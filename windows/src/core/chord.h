#pragma once
#include <bitset>
#include <cstdint>
#include <functional>

#include "core/clock.h"

namespace ixpat {

// Virtual-key codes the mouse's keyboard collection sends (Raw Input reports VK_SHIFT for both shifts).
constexpr int kVkShift = 0x10, kVkLWin = 0x5B, kVkRWin = 0x5C, kVkLShift = 0xA0, kVkRShift = 0xA1;
constexpr int kVkS = 0x53, kVkF23 = 0x86;

enum class ChordOutput : std::uint8_t { none, press, doublePress, holdBegin, holdEnd };

/// Recognises the action button from the mouse's own keyboard and mouse reports, ported from
/// ActionButtonController. The firmware decides the gesture and sends a Windows chord for each:
///   press        -> Win+Shift+S            (Snipping Tool)
///   double press -> Win + a fake LEFT CLICK (Click to Do)
///   hold         -> Win+Shift+F23 held     (Copilot key), released when the button is
/// Feed it the mouse's keys only: its Shift and Win once Raw Input attributes them to the mouse's
/// keyboard collection, its letters as the keyboard hook swallows them while the Win chord is in
/// flight, and left presses from the mouse's own mouse collection. It fires only on keys it was fed:
/// a chord Windows already acted on (the hooks were down) never fires here as well.
class ChordDecoder {
public:
    /// Asked when a gesture is recognised (the letter of a press or hold, the fake click of a double press);
    /// return true to consume it (the Action Ring is open in toggle mode and this commits it). The chord's
    /// remaining keys stay hidden. A bare Win make never asks: Windows may still act on what follows.
    std::function<bool()> pressIntercept;

    ChordOutput key(int vk, bool down, Seconds now);
    ChordOutput mouseLeftDown(Seconds now);
    /// Consume the chord in flight (or the next one while these keys stay down) without firing it.
    void consumePress(Seconds now);

    /// A recognised chord is held (schematic glow).
    bool pressed() const { return chord_ != Chord::none && !consumed_; }
    bool pressed(Seconds now) const { return pressed() && !stale(now); }
    /// The mouse's Win chord is down: a letter (S / F23) now completes it and must not reach Windows.
    bool inFlight(Seconds now) const { return chord_ != Chord::none && !stale(now); }
    /// Keys from the mouse are in flight or just were.
    bool chordActive(Seconds now) const { return now < deviceKeyUntil_ || inFlight(now); }
    /// The fake click of the double-press chord is pending or recent: the hook swallows left clicks. Shift in the
    /// chord means a press or hold instead, and a click then is the user's own.
    bool swallowLeftClick(Seconds now) const { return (chord_ == Chord::win && !shiftInChord_ && !stale(now)) || now < swallowClicksUntil_; }
    /// A chord whose release never arrived (a lost break) is dropped once the mouse has been quiet this long.
    bool stale(Seconds now) const { return (chord_ != Chord::none || down_.any()) && now > deviceKeyUntil_ - kKeyWindow + kStaleAfter; }
    void reset();

private:
    enum class Chord : std::uint8_t { none, snip, win, copilot };
    static constexpr Seconds kKeyWindow = 0.4;    // after any mouse key event
    static constexpr Seconds kClickWindow = 0.5;  // after the fake click (or the Win chord that promised it)
    static constexpr Seconds kStaleAfter = 1.5;   // no key from the mouse for this long: whatever is "down" is lost
    void dropIfStale(Seconds now);

    ChordOutput chordDown(Chord c, Seconds now);
    ChordOutput chordUp(Seconds now);
    bool winDown() const { return down_[kVkLWin] || down_[kVkRWin]; }
    bool shiftDown() const { return down_[kVkShift] || down_[kVkLShift] || down_[kVkRShift]; }

    std::bitset<256> down_;
    Chord chord_ = Chord::none;
    bool clickSeen_ = false;
    bool consumed_ = false;
    bool shiftInChord_ = false;  // Shift took part in the Win chord: it is a press or a hold, never the click chord
    bool clickFires_ = false;    // the Win chord was released before its fake click: that click is the double press
    Seconds deviceKeyUntil_ = 0;
    Seconds swallowClicksUntil_ = 0;
};

}  // namespace ixpat

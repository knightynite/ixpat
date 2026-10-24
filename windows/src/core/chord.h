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
/// Feed it only keys that Raw Input attributes to the mouse's keyboard collection, never the user's
/// keyboard, and only left-button presses from the mouse's own mouse collection.
class ChordDecoder {
public:
    /// Asked when a chord is recognised; return true to consume it (the Action Ring is open in
    /// toggle mode and this press commits it). The chord's keys and fake click stay hidden.
    std::function<bool()> pressIntercept;

    ChordOutput key(int vk, bool down, Seconds now);
    ChordOutput mouseLeftDown(Seconds now);
    /// Time-based decisions: a Win chord held past kHoldAfter with no S / F23 make is a hold; a Win chord
    /// released with nothing after it is a double press once the grace is over. Call when nextDeadline() passes.
    ChordOutput poll(Seconds now);
    /// When poll() needs to run next (0 = nothing pending).
    Seconds nextDeadline() const;
    /// Consume the chord in flight (or the next one while these keys stay down) without firing it.
    void consumePress(Seconds now);

    /// A recognised chord is held (schematic glow).
    bool pressed() const { return chord_ != Chord::none && !consumed_; }
    bool pressed(Seconds now) const { return pressed() && !stale(now); }
    /// Keys from the mouse are in flight: the hook swallows Win-chord key events.
    bool chordActive(Seconds now) const { return now < deviceKeyUntil_ || (chord_ != Chord::none && !stale(now)); }
    /// The fake click of the double-press chord is pending or recent: the hook swallows left clicks.
    bool swallowLeftClick(Seconds now) const { return (chord_ == Chord::win && !stale(now)) || decisionPending_ || now < swallowClicksUntil_; }
    /// A chord whose release never arrived (a lost break) is dropped once the mouse has been quiet this long.
    bool stale(Seconds now) const { return (chord_ != Chord::none || down_.any()) && now > deviceKeyUntil_ - kKeyWindow + kStaleAfter; }
    void reset();

private:
    enum class Chord : std::uint8_t { none, snip, win, copilot };
    static constexpr Seconds kKeyWindow = 0.4;    // after any mouse key event
    static constexpr Seconds kClickWindow = 0.5;  // after the fake click (or the chord that promised it)
    static constexpr Seconds kStaleAfter = 1.5;   // no key from the mouse for this long: whatever is "down" is lost
    static constexpr Seconds kHoldAfter = 0.3;    // Win+Shift still down this long with no S: the firmware's hold (its F23 make was lost)
    static constexpr Seconds kDecisionGrace = 0.05;  // after a Win chord's release: wait for the S break or the fake click
    void dropIfStale(Seconds now);

    ChordOutput chordDown(Chord c, Seconds now);
    ChordOutput chordUp(Seconds now);
    bool winDown() const { return down_[kVkLWin] || down_[kVkRWin]; }
    bool shiftDown() const { return down_[kVkShift] || down_[kVkLShift] || down_[kVkRShift]; }

    std::bitset<256> down_;
    Chord chord_ = Chord::none;
    bool clickSeen_ = false;
    bool consumed_ = false;
    Seconds deviceKeyUntil_ = 0;
    Seconds swallowClicksUntil_ = 0;
    // Chords whose letter make never arrived (Raw Input drops the S / F23 make of the firmware's chords).
    Seconds winSince_ = 0;          // when the Win chord began
    bool holdByTime_ = false;       // promoted to a hold by kHoldAfter
    bool decisionPending_ = false;  // a Win chord was released: press, double press or hold decided by what follows
    bool pendingConsumed_ = false;
    Seconds decideAt_ = 0;
    ChordOutput queued_ = ChordOutput::none;  // a second output for the next poll (hold begin + end)
};

}  // namespace ixpat

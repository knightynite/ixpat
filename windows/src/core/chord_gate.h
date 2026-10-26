#pragma once
#include <array>
#include <bitset>
#include <cstdint>

#include "core/chord.h"
#include "core/clock.h"

namespace ixpat {

/// The decisions the input thread makes about the action button's chord keys, with no Win32 in them, so the
/// firmware's sequences can be replayed in tests exactly as the logs recorded them.
///
/// Who hears what: Windows runs the keyboard hook before it generates a key's Raw Input report, and generates
/// none for a key the hook ate. So the hook decides alone (hookKey), the report names the device a millisecond
/// later (rawKey). Device-confirmed modifiers feed the decoder even during hook recovery. The mouse's Shift and Win pass
/// (a mask key keeps the Win release from opening Start), its letters are swallowed once its Win chord is in
/// flight. A letter first seen through Raw Input has already reached Windows and never fires an action here.
class ChordGate {
public:
    struct Config {
        bool swallowChords = true;  // hide the chords from Windows
        bool enabled = true;
    };
    struct Decision {
        bool swallow = false;                   // the hook returns 1
        ChordOutput out = ChordOutput::none;    // what the decoder fired
        bool sendMask = false;                  // Windows holds the mouse's Win: send the mask key now
        bool hookSaw = false;                   // rawKey: a callback vouched for this report
    };

    ChordGate();

    /// The keyboard hook: a physical event of a chord key (vk as the hook reports it: VK_LSHIFT, VK_LWIN, S, F23).
    Decision hookKey(int vk, bool down, std::uint16_t scan, Seconds now, const Config& cfg);
    /// Raw Input: a physical report of a chord key (vk as Raw Input reports it: VK_SHIFT for either shift).
    Decision rawKey(int vk, bool down, std::uint16_t scan, bool fromMouse, Seconds now, const Config& cfg);
    /// The mouse hook: a physical left press attributed to the mouse. `ringOpen`: the click is the ring's instead.
    Decision leftDown(Seconds now, const Config& cfg, bool ringOpen);

    /// Windows' own view of the letters (GetAsyncKeyState) settles their ownership after missed events.
    void reconcileLetters(bool sHeld, bool f23Held);
    /// Time passed: a chord whose breaks never came is forgotten; a hold that expires still ends (holdEnd).
    ChordOutput tick(Seconds now);
    /// Nothing in flight and nothing owed: the hooks may be refreshed.
    bool idle(Seconds now) const;

    ChordDecoder& decoder() { return chord_; }
    const ChordDecoder& decoder() const { return chord_; }
    bool letterPassed(int vk) const { return letterPassed_[static_cast<std::size_t>(vk)]; }
    bool swallowedMake(int vk) const { return swallowedMake_[static_cast<std::size_t>(vk)]; }

    static int chordIndex(int vk);
    static bool isChordKey(int vk);
    static bool isLetter(int vk) { return vk == kVkS || vk == kVkF23; }

private:
    static constexpr Seconds kHookSeenWindow = 0.1;     // a report pairs with its callback's observation within this
    static constexpr Seconds kForgetSwallowed = 2.0;    // a swallowed make whose break never came: the key is the user's again after this

    // What the hook saw of each chord key, by chordIndex: one observation, consumed by the one report it belongs to.
    struct HookSeen {
        Seconds at = -1e9;
        std::uint16_t scan = 0;
        bool used = true;
    };
    std::array<HookSeen, 256> hookMake_{}, hookBreak_{};
    std::bitset<256> swallowedMake_;  // the hook ate this make: its break is eaten too
    std::bitset<256> fedMake_;        // the decoder heard this make: it hears the break
    std::bitset<256> letterPassed_;   // Windows holds this letter from a make the hook passed: nothing of it is ever eaten
    std::array<Seconds, 256> swallowedAt_{};
    ChordDecoder chord_;
};

}  // namespace ixpat

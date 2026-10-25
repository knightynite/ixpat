#include "core/chord_gate.h"

namespace ixpat {

ChordGate::ChordGate() { swallowedAt_.fill(-1e9); }

int ChordGate::chordIndex(int vk) {
    const int v = vk & 0xFF;
    return (v == kVkLShift || v == kVkRShift) ? kVkShift : v;
}

bool ChordGate::isChordKey(int vk) {
    return vk == kVkLWin || vk == kVkRWin || vk == kVkShift || vk == kVkLShift || vk == kVkRShift || vk == kVkS || vk == kVkF23;
}

void ChordGate::hooksReinstalled() {
    for (auto& s : hookMake_) s.used = true;
    for (auto& s : hookBreak_) s.used = true;
}

void ChordGate::reconcileLetters(bool sHeld, bool f23Held) {
    // A letter Windows holds came from a make the hook never saw, so it is the user's, release and all; a letter
    // Windows does not hold is nobody's, whatever a lost break left behind.
    for (const auto [vk, held] : {std::pair{kVkS, sHeld}, std::pair{kVkF23, f23Held}}) {
        const auto slot = static_cast<std::size_t>(vk);
        if (held && swallowedMake_[slot]) {
            swallowedMake_[slot] = false;
            letterPassed_[slot] = true;
        }
        if (!held && letterPassed_[slot]) letterPassed_[slot] = false;
    }
}

ChordOutput ChordGate::tick(Seconds now) {
    // The decoder forgets a chord whose breaks never came once the mouse has been quiet; the bookkeeping that
    // mirrors it goes with it, so the next chord starts clean and a break that still turns up passes as the user's.
    if (!chord_.stale(now)) return ChordOutput::none;
    const ChordOutput out = chord_.expire(now);
    fedMake_.reset();
    swallowedMake_.reset();
    return out;
}

bool ChordGate::idle(Seconds now) const {
    return !chord_.chordActive(now) && fedMake_.none() && swallowedMake_.none() && letterPassed_.none();
}

ChordGate::Decision ChordGate::hookKey(int vk, bool down, std::uint16_t scan, Seconds now, const Config& cfg) {
    Decision d;
    if (!isChordKey(vk)) return d;
    tick(now);  // a stale chord is gone before this key is judged (the caller reports a holdEnd from its own tick)
    const int idx = chordIndex(vk);
    const auto slot = static_cast<std::size_t>(idx);
    (down ? hookMake_ : hookBreak_)[slot] = HookSeen{now, scan, false};
    // A make the hook ate whose break never came (the mouse went to sleep mid-chord): after a while the key is the
    // user's again, whichever way its next event goes.
    if (swallowedMake_[slot] && now - swallowedAt_[slot] > kForgetSwallowed) swallowedMake_[slot] = false;
    if (!down) {
        // A break follows its make. A letter the hook ate stays hidden to the end, and the decoder hears the
        // break of every letter it heard the make of; a letter Windows holds is released to Windows. Shift and Win
        // breaks pass, and their report feeds the decoder a moment later.
        d.swallow = swallowedMake_[slot];
        swallowedMake_[slot] = false;
        letterPassed_[slot] = false;
        if (isLetter(idx) && fedMake_[slot]) {
            fedMake_[slot] = false;
            d.out = chord_.key(idx, false, now);
        }
        return d;
    }
    if (!isLetter(idx)) {
        // Shift and Win makes pass: Raw Input names their device a millisecond later. One exception: the mouse's Win,
        // held past the typematic delay, comes round again as a repeat make. A chord action may have sent Win's
        // release meanwhile (Win+Tab for Task View), so Windows would take the repeat for a fresh press and open
        // Start on the real release. The repeat is eaten; Shift repeats are harmless and pass.
        if (down && (idx == kVkLWin || idx == kVkRWin) && fedMake_[slot]) d.swallow = true;
        return d;
    }
    // S or F23 while the mouse's Win chord is in flight completes it. Hidden from Windows by default, and the decoder
    // hears it from here either way. A letter Windows already holds (the user's own S, typed before the chord) is the
    // user's: it keeps passing, repeats and release alike, and the chord goes without it. A classified press or hold
    // takes only its own letter.
    if (letterPassed_[slot]) {
        if (fedMake_[slot]) d.out = chord_.key(idx, true, now);  // a letter that passed but completed the chord: repeats keep it fresh
    } else if (swallowedMake_[slot]) {
        d.swallow = true;  // a repeat of a make the hook ate: hidden, and it keeps the chord fresh and owned
        swallowedAt_[slot] = now;
        d.out = chord_.key(idx, true, now);
    } else if (chord_.inFlight(now) && chord_.wantsLetter(idx)) {
        d.swallow = cfg.swallowChords && cfg.enabled;
        swallowedMake_[slot] = d.swallow;
        swallowedAt_[slot] = now;
        letterPassed_[slot] = !d.swallow;
        fedMake_[slot] = true;
        d.out = chord_.key(idx, true, now);
    } else {
        letterPassed_[slot] = true;  // the user's own, also beside a chord that already has its letter
    }
    return d;
}

ChordGate::Decision ChordGate::rawKey(int vk, bool down, std::uint16_t scan, bool fromMouse, Seconds now, const Config& cfg) {
    Decision d;
    if (!isChordKey(vk)) return d;
    // The one callback that belongs to this report vouches for it, once, whichever device sent it: a report from
    // another keyboard consumes its observation too, so it can never vouch for a later report of the mouse.
    const int idx = chordIndex(vk);
    const auto slot = static_cast<std::size_t>(idx);
    HookSeen& seen = (down ? hookMake_ : hookBreak_)[slot];
    d.hookSaw = !seen.used && now - seen.at >= 0 && now - seen.at < kHookSeenWindow && (seen.scan == scan || scan == 0);
    if (d.hookSaw) seen.used = true;
    if (!fromMouse) return d;
    tick(now);
    if (!d.hookSaw) return d;  // the hook is gone and Windows has already acted: silence
    if (down) {
        if (fedMake_[slot]) {
            d.out = chord_.key(vk, true, now);  // a typematic repeat: the decoder ignores it but stays fresh
            return d;
        }
        fedMake_[slot] = true;
        d.out = chord_.key(vk, true, now);
        // Windows holds this Win down too (it had to pass for Raw Input to name its device). A key that means
        // nothing, between its make and its break, keeps the Start menu shut on the release. Every Win make of a
        // chord gets one: the second press of a double press, or a press right after another, arrives with the
        // chord already in flight, and its release would open Start just the same.
        if ((vk == kVkLWin || vk == kVkRWin) && chord_.inFlight(now) && cfg.swallowChords && cfg.enabled) d.sendMask = true;
    } else {
        if (!fedMake_[slot]) return d;
        fedMake_[slot] = false;
        d.out = chord_.key(vk, false, now);
    }
    return d;
}

ChordGate::Decision ChordGate::leftDown(Seconds now, const Config& cfg, bool ringOpen) {
    Decision d;
    if (!chord_.swallowLeftClick(now)) return d;
    // The fake click of the double-press chord, or one in its wake. With the ring open the click is the ring's
    // commit (the decoder asks it through pressIntercept; a late click is handed over by the caller).
    d.out = chord_.mouseLeftDown(now);
    d.swallow = ringOpen || (cfg.swallowChords && cfg.enabled);
    return d;
}

}  // namespace ixpat

# Plan 7: the hooks decide alone

## Why

Three live tests with the real mouse (firmware 6.14) ended the same way: the first press of the action button
after a pause opened the Snipping Tool *and* the Action Ring, the second press worked. The report log made the
mechanism plain.

- Every failed press arrived with **no keyboard-hook callback at all**: Windows had dropped the hook. The chord
  went to Windows (Snipping Tool), and the decoder, fed from Raw Input, fired the ring. Both.
- Windows runs the low-level keyboard hook **before** it generates a key's Raw Input, and generates none for a
  key the hook ate. The Shift callback waited 2 ms draining `WM_INPUT` and saw nothing; the Shift report landed
  1 ms after the callback returned. Waiting inside the keyboard hook for the report can never succeed; it only
  stalled the system's input thread, 2 ms per wheel notch and up to 15 ms per chord key, which is also why a
  spin of the wheel stuttered.
- The mouse's letter (S / F23) **never appears in Raw Input**, hook or no hook. Only its Shift and Win do.
- The liveness check misfired because for mouse events the report precedes the callback by microseconds and
  for keyboard events it follows it, so "report newer than callback" is normal, not a dead hook.

## What changed

**Hooks never wait.** The mouse hook drains the reports already queued (the mouse's reports precede it) and
decides from them, else from the usual rule (this mouse live, no other device recently). The keyboard hook
passes Shift and Win, swallows S / F23 while the mouse's Win chord is in flight, and swallows the break of every
make it swallowed.

**Raw Input names the device, and never fires.** A Shift or Win report from the mouse's keyboard collection
feeds the decoder only if the hook saw that key within 30 ms. The first Surface Win make arms the chord; from
then on the hook eats the letters and feeds them to the decoder itself. A report the hook never saw means the
hook is gone and Windows has already acted: the decoder stays silent, so the stock behaviour happens once,
never twice.

**The Start menu stays shut.** Windows holds the mouse's Win down too (it had to pass for Raw Input to name its
device). When the chord arms, a key that means nothing (vk 0xFF, no scancode) is sent, so the Win release is not
a lone Win press.

**The decoder lost its guesses.** The time-based fallbacks (a press from an S break without its make, a hold
after 0.3 s, a double press when a grace period ended) existed because Raw Input dropped the letter makes; the
hook sees them directly. They were also exactly how a chord Windows had handled fired a second time. The
decoder now fires only on keys it was fed. A Win chord with Shift and no letter is a press or hold that went to
Windows: nothing fires, no click is hidden. A Win chord without Shift is the click chord, and its fake click is
still hidden and still fires for half a second after the release.

**Liveness, per hook.** A physical report with no callback from its hook within 30 ms around it starts a
50 ms clock; if the callback never comes, the hooks are re-installed and the event is logged with the reason.
Keyboard reports check the keyboard hook, mouse reports the mouse hook.

**Measurements for the drop.** Why Windows drops the hooks is still open: our callbacks peak at 2 ms. New in
Diagnostics and in the re-install log line: the longest time an event queued before reaching the callback
(`time` in the hook struct), the slowest pass down the chain (`CallNextHookEx` runs other programs' hooks, and
Logitech Options+ is one of them), the last `SetWindowsHookEx` error, and the longest gap between smooth-scroll
frames.

**Wheel.** The coast frame clock is started by the first notch and keeps its phase; restarting it on every notch
stretched frames to 16 ms. Sliders in the settings window step once per whole notch under the smooth stream.

## Live-test protocol

Press once, wait, press twice, wait, hold. Expected: ring on press (no Snipping Tool, no Start menu), Task View
on double press, the hold action on hold. Then type `s` and press Win on the keyboard: both must behave as
stock. The report log shows `hook vk .. passed/swallowed` beside `keyboard vk ..`; a `(no hook callback)` tag
means the hook was down for that report, and a `hooks re-installed` line in the debug log follows within 50 ms.

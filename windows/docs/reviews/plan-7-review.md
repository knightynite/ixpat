# Plan 7 review (independent review, 2026-10-24) and the triage

Read-only review of the input rework (`plan-7-input-rework.md`): the hooks that decide alone, the Raw Input path that
only names devices, the liveness check, the mask key and the decoder without its time-based guesses. The reviewer
had the design note, the full diff and the two files; it did not build or run anything. Every finding was checked
against the code before acting.

## Findings and what happened

| # | Severity | Finding | Verdict | Action |
|---|---|---|---|---|
| 1 | Blocker | A letter the user typed before the chord (Windows holds its make) could be claimed by the chord: its repeats swallowed, then its break, leaving Windows with S stuck down. A user's S during a hold could also turn the hold into a press | confirmed | `letterPassed_`: a letter whose make the hook passed stays the user's to the end (repeats and release pass, nothing fed). The decoder no longer changes a classified press or hold on another letter (tested) |
| 2 | Should fix | When the decoder dropped a stale chord, the hook's own bookkeeping (`fedMake_`, `swallowedMake_`) stayed set: the next chord's modifiers were discarded as repeats, and a lost S break left the next typed `s` swallowed | confirmed | `forgetStaleChord()` clears both when the decoder goes stale; a swallowed make older than 2 s releases its key on the next break |
| 3 | Should fix | Modifier repeats from the mouse were dropped before the decoder, so a long hold could go stale while the device was still reporting | confirmed | Attributed repeats are fed (the decoder ignores them but stays fresh) |
| 4 | Should fix | The `WM_INPUT` drain inside a hook callback pumps the queue, and another hook callback can run inside it, mutating the same state out of order | confirmed | The drain uses `PM_QS_INPUT`: only input-queue messages are taken, sent messages (how a nested callback arrives) stay put |
| 5 | Should fix | The mask key was sent from the loop's tail; a Win release passing in between opened the Start menu | confirmed | The mask is sent as the callback that learned of the chord returns, and the hook flushes it before passing any Win release |
| 6 | Should fix | vk 0xFF is outside `KEYBDINPUT`'s documented 1..254, and `SendInput`'s result was ignored | confirmed | vk 0xE8 (documented unassigned); failures are counted and shown in Diagnostics |
| 7 | Should fix | Liveness false positive: a 40 ms pass down the chain made the report look unanswered. False negative: a hook lost right after a callback, with the next reports inside the 30 ms window, was never revisited | confirmed | The callback's end also counts as "alive". While idle, the hooks are put back every 5 s regardless, so a lost hook never waits for a key. The wait is 50 ms as documented |
| 8 | Should fix | `hookSaw` compared a report against the last callback time of that key: a keyboard Win callback could vouch for a later mouse Win report; negative gaps were accepted | confirmed | One observation per key and direction, consumed by the one report it belongs to, matched by scan code within a forward-only window; all observations are voided when the hooks are re-installed |
| 9 | Should fix | With the ring open, the bare Win make already asked the ring to commit, so a chord Windows then handled still committed the ring | confirmed | The ring is asked at the letter (press, hold) or the fake click (double press), never at Win alone (tested) |
| 10 | Should fix | With Win+Shift down (a press or hold on its way) a real left click was swallowed and fired a double press | confirmed | Shift in the chord rules the fake click out for swallowing and for firing; `consumePress` opens a click window only for the click chord (tested) |
| 11 | Should fix | A hook callback took a shared lock that main holds exclusively while copying the config | confirmed | Published config snapshots: main writes the next slot and publishes its index, a callback copies the published slot and never waits |
| 12 | Minor | `notches * 120.0f` mixes int and float | compiles clean under `/W4 /WX` | Cast added anyway |
| 13 | Minor | A comment had the mouse order backwards | confirmed | Fixed |

Also confirmed by the review: typing `s`, pressing Win alone and Win+Shift+S from the keyboard never arm the decoder;
another device's Shift never touches the mouse's Shift state; swallowed letter breaks stay hidden even if hiding is
switched off between make and break; the decoder no longer invents a press from an orphan break or a hold from a
timer; `CallNextHookEx` is called once on the pass path and skipped on the swallow path; the busy-waits are gone and
a running coast keeps its clock.

Added alongside the triage: a system stall watch (a thread that measures how late its 50 ms sleeps wake). Windows
drops a low-level hook it could not deliver a callback to in time and tells nobody; the hooks were still being
dropped with our callbacks at 18 µs and the chain at 59 µs, so the remaining suspect is the machine itself standing
still. A stall of 100 ms or more is logged, shown in Diagnostics, and followed by an immediate re-install.

Measured after the triage, with a standalone probe (a process that installs both hooks and prints every callback):
input injected by another process (`keybd_event`, `mouse_event`, `SendInput`) reached Raw Input but never a low-level
hook, on this machine, at equal integrity. So the main thread's watchdog now compares hook calls with *physical*
reports only; its earlier "raw input flowing but no keyboard hook callbacks" lines during test runs were the test
harness's own injected keys, not dropped hooks. The two genuine drops in the logs (the first press after a long idle,
twice) remain unexplained and covered by the re-install paths.

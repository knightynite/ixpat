# Plan 1 review (Codex gpt-6-astra, 2026-10-22)

Independent read-only review of the Plan 1 diff (core engine port + probe), fed inline. 16 findings; the
triage below is the author's. Fixed items have a test that failed before the fix.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | Important | Probe hooks allocate (device strings, deque, map) and the drain is unbounded | Fixed in part: drain bounded to 64 messages, injected entries excluded; device-name resolution still allocates once per new device. The probe is an instrument, not the product; the product's input thread uses a fixed device table and a fixed-size RAWINPUT buffer. |
| 2 | Important | Probe attribution matched by kind only; packets with motion + buttons lost entries; injected events consumed physical entries | Fixed: packets expanded into separate entries; content matching (wheel delta, button flag, scan code + up/down); injected hook events are never attributed. |
| 3 | Important | `matchedViaDrain` inferred from queue size | Fixed: each raw entry carries drain provenance; the matched entry's flag is reported. |
| 4 | Important | Numbers narrowed before clamping (`2147483648` -> negative -> 0; `1e300` UB) in settings, rules, bridge intensity | Fixed: `getOr<int>/<uint32_t>/<double>` read as double, reject non-finite, clamp into range before narrowing; rules/echoes/actions use `requiredInt`/`optionalInt`; bridge intensity clamped from double. Tests in `test_settings.cpp`. |
| 5 | Important | Decode leniency differs from Swift (which rejects the whole snapshot on a type error) | **Declined (ruling).** The Windows app keeps a user's valid settings and falls back per field; the Mac throws everything away and keeps a `.broken.json`. Per-field fallback loses less. Documented in `design.md` 3.4. Required action payload fields are now range-checked; invalid UUIDs are regenerated. |
| 6 | Important | Bridge: no lowercasing, no alias canonicalisation in the response, no 32-char limit, non-integer intensity returned 400 instead of Swift's 3 | Fixed, all four, with tests. Reverses the Task 10 ruling: the plugin contract is the Mac's. |
| 7 | Important | Chord recognition depended on make order (`Win, S, Shift` never recognised) | Fixed: the full key set is evaluated on every make; the `win` chord is only entered when no S/F23 is down. Test added for both odd orders. |
| 8 | Minor | Key-up extends the 400 ms window; Win release without a click adds 500 ms click suppression (not in Swift) | **Declined (ruling).** Windows delivers a chord's key-ups individually and the fake click can arrive after the Win release; both windows protect apps from a stray Win key or click. Pinned by tests on purpose. |
| 9 | Minor | Preset names used base rules; Swift uses effective rules (profile override reads Custom) | Fixed with test. |
| 10 | Minor | Sparse `actions` map is written out with explicit `{"none":{}}` entries | **Deferred.** The Mac reads `{"none":{}}` as "do nothing", the same as a missing key. |
| 11 | Minor | `GestureConfig` defaults were `none` | Fixed: Swift's defaults (volume up/down, previous/next track), test added. |
| 12 | Minor | Wheel state froze while disabled (re-enabling mid-scroll read as a pause) | Fixed: state advances, pulses are gated; test added. |
| 13 | Minor | Probe HID buffers used `max(len, min)` and ignored `written` | Fixed: exact descriptor lengths, descriptors shorter than expected are rejected, `WriteFile` requires a full write. |
| 14 | Minor | `QPC * 1e6` overflow after ~10 days | Fixed: subtract first, long double conversion. |
| 15 | Minor | Unknown waveform names returned views into rotating storage | Fixed: `waveformName` / `Waveform::name` return owning strings; test added. |
| 16 | Minor | `from_chars` accepted numeric prefixes ("2oops" = 2) | Fixed with test. |

Also noted by the reviewer and confirmed: nothing writes to Col04/Col05; the CFU "versions" lookup is by HID
usage 0x62 (the INF's `VersionsFeatureValueCapabilityUsageRangeMinimum`), resolved to a report ID through the
value caps, which is the intended reading.

Missing tests the reviewer asked for and their status: a Swift-generated golden corpus (pending: needs a file
written by the Mac app; the hand-built fixture stands in), chord order/boundary traces (partly added),
an allocation-counting probe harness (not planned; the product thread is the one that matters).

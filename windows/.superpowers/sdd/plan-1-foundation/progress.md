# SDD ledger — plan: windows/docs/plan-1-foundation.md
Spec: windows/docs/design.md (read). Branch: windows-port (main stays at origin/main).
Pre-flight (shared interfaces):
- T2 -> T3/T4: HapticRule/HapticEcho defined in events.h, consumed by json_compat and Settings. OK.
- T3 -> T4: ButtonAction/RingItem/AppProfile + to_json/from_json consumed by Settings JSON. OK.
- T4 -> T5: Settings fields rules/scrollStepEvery/ratchet*/freeSpin* consumed by presets. OK.
- T2/T4 -> T7: WheelFeelParams takes ordinals, not Settings; Router wiring deferred to Plan 2. OK.
- T3 -> T9: GestureConfig uses ButtonAction. OK.
- T11 -> T10: bridge waveformsJson is a std::function; no type coupling. OK.
Ruling: Task 1 — add_library needs a source; added src/core/core.cpp (windows.h guard) — cost if wrong: none.

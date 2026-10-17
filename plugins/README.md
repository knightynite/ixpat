# iXpat plugin bridge

iXpat listens on `http://127.0.0.1:47337` (loopback only). Any app plugin or
script can ask for a pulse:

```sh
curl -s -X POST http://127.0.0.1:47337/haptic \
  -H 'content-type: application/json' \
  -d '{"token":"<from Diagnostics>","event":"align","intensity":3}'
curl -s http://127.0.0.1:47337/waveforms
```

`event` is a waveform (`hover`, `collide`, `align`, `step`, `grow`, `success`,
`error`, `click`) or an alias (`snap`, `guide`, `detent`, `tick`, `limit`,
`done`, `fail`, `notify`, `select`, `press`). Intensity 1…4, scaled by the
master strength and gated by the Haptics switch.

- `photoshop-uxp/`: a sample UXP plugin (untested here; Photoshop is not
  installed on the development Mac).

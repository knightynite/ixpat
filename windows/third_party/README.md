# Vendored headers

Header-only, pinned, checked in. Update by downloading the exact release file and recording the new hash here.

| Library | Version | File | Source | SHA-256 |
|---|---|---|---|---|
| nlohmann/json | 3.12.0 | `nlohmann/json.hpp` | https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp | `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63` |
| doctest | 2.5.3 | `doctest/doctest.h` | https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h | `CFD518A3EF90F67E1F3BA514DF23FB3627437DE1A2FEEBA78CF5062A40021421` |

Verify: `Get-FileHash windows/third_party/nlohmann/json.hpp, windows/third_party/doctest/doctest.h -Algorithm SHA256`

Both are MIT licensed. The directory is included as a system include path (`/external:W0`), so their
warnings do not count against the project's `/W4 /WX`.

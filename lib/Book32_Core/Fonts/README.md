# Reader font families

These are pre-rendered Adafruit-GFX bitmap fonts (`GFXfont`), generated with
the `fontconvert` tool from Adafruit-GFX-Library, at 9/12/18pt (Regular) and
9/12/18/24pt (Bold). `FreeSans.h`/`.cpp` additionally carries Regular 24pt,
replacing the ASCII-only Adafruit `<Fonts/FreeSans*pt7b.h>` headers across the
whole firmware.

## Charset and naming

All fonts cover **0x20-0xFF** (ASCII + Latin-1 Supplement), so Portuguese and
other Western European text renders correctly. `fontconvert` names fonts with
the `pt8b` suffix when the charset goes beyond 7-bit ASCII, hence
`Merriweather_Regular12pt8b` etc.

Regenerate with: `fontconvert <ttf> <size> 32 255`

**yAdvance is intentionally patched** after generation to the project's
established per-family line heights (TTF face metrics differ between mirrors
and builds). If you regenerate, re-apply the values currently in the `.cpp`
files or pagination and UI layout will shift.

## Layout: `.h` + `.cpp`

Each family ships as an `extern` declarations header plus a `.cpp` with the
actual bitmap data. Font data in headers gets duplicated into every translation
unit that includes them (C++ `const` has internal linkage), which wastes flash;
the split guarantees a single copy.

They were chosen as freely-licensed substitutes for two proprietary,
non-redistributable typefaces:

| Header             | Font (as embedded)      | Substitute for   | License                  |
|---------------------|--------------------------|------------------|---------------------------|
| `Merriweather.h/.cpp` | Merriweather            | Bookerly (Amazon, proprietary) | SIL Open Font License 1.1 |
| `Literata.h/.cpp`   | Literata                  | (requested directly) | SIL Open Font License 1.1 |
| `SourceSerif4.h/.cpp` | Source Serif 4          | Source Serif Pro (renamed by Adobe) | SIL Open Font License 1.1 |
| `Gelasio.h/.cpp`    | Gelasio                   | Georgia (Microsoft, proprietary) | SIL Open Font License 1.1 |
| `FreeSans.h/.cpp`   | GNU FreeFont FreeSans     | Adafruit FreeSans (ASCII-only) | GPLv3 with font exception |

Source TTFs: [google/fonts](https://github.com/google/fonts) (`ofl/` directory).
Variable font instances were pinned to static Regular (wght=400) and Bold
(wght=700) weights with `fonttools varLib.instancer` before conversion.

Full OFL 1.1 license text: https://openfontlicense.org

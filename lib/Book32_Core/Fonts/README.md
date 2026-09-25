# Reader font families

These are pre-rendered Adafruit-GFX bitmap fonts (`GFXfont`), generated with
the `fontconvert` tool from Adafruit-GFX-Library, at 10/12/14/16/18/20pt
(Regular) and 10/12/14/16/18/20/24pt (Bold). `FreeSans.h`/`.cpp` additionally
carries Regular 9pt and Bold 9pt (used by the on-device system UI via
`FontMgr`, not by the reader), replacing the ASCII-only Adafruit
`<Fonts/FreeSans*pt7b.h>` headers across the whole firmware.

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
| `OpenSans.h/.cpp`   | Open Sans                 | (extra sans-serif option, not a substitute) | SIL Open Font License 1.1 |

Source TTFs: [google/fonts](https://github.com/google/fonts) (`ofl/` directory,
the variable `[wght]`/`[opsz,wght]`/etc. file in each family folder — none of
these five families ship a pre-instanced `static/` subfolder). Variable font
instances were pinned to the "Regular" and "Bold" named instances
(wght=400/700, plus each family's fixed `opsz`/`wdth` where it has one — see
each font's `fvar` table) with `fonttools varLib.instancer` before conversion.
Their generated yAdvance came out identical to this project's previously
established per-size line heights (verified at the sizes that predate this
9-value -> 6-value size-set change: 12/18/24pt for all five, plus what was
9pt for OpenSans/Merriweather/Literata/SourceSerif4/Gelasio before the reader
dropped that size), so none of the six families needed a manual yAdvance
patch this time around — the note above is about *future* regenerations from
a different TTF mirror or FreeType build, which can (and, for FreeSans, did)
drift.

`FreeSans.h`/`.cpp` is the one exception: its 10/14/16/20pt sizes were
generated from a different FreeSans mirror
([opensourcedesign/fonts](https://github.com/opensourcedesign/fonts), since
the original Debian `fonts-freefont-ttf` package isn't reachable from every
build environment) than the 9/12/18/24pt sizes already in the file, and that
mirror's own yAdvance output diverged from the established anchors outside
of 12pt (which matched by coincidence). Its four new sizes are patched to
values linearly interpolated between the established 9/12/18/24pt anchors
instead — see the header comment in `FreeSans.h`.

Full OFL 1.1 license text: https://openfontlicense.org

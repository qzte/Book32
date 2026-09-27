# Reader font families

These are pre-rendered Adafruit-GFX bitmap fonts (`GFXfont`), generated with
the `fontconvert` tool from Adafruit-GFX-Library, at 9/10/11/12/13/14/16pt
(Regular) and 9/10/11/12/13/14/16/24pt (Bold). `FreeSans.h`/`.cpp`
additionally carries Regular 18pt and Bold 18pt (used by the on-device
system UI via `FontMgr`, not by the reader — see `FontMgr::getFont()`),
replacing the ASCII-only Adafruit `<Fonts/FreeSans*pt7b.h>` headers across
the whole firmware.

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
established per-size line heights at every size carried over from an earlier
size-set (9/10/12/14/16/24pt for all five — the reader has gone through two
size-set changes: an original 3-value pt scheme, a 6-value px scheme, and
now this 7-value px scheme), so the two sizes newly added here (11pt, 13pt)
also came out of the pipeline with no manual yAdvance patch needed — the
note above is about *future* regenerations from a different TTF mirror or
FreeType build, which can (and, for FreeSans, did) drift.

`FreeSans.h`/`.cpp` is the one exception: its 10/11/13/14/16pt sizes were
generated from a different FreeSans mirror
([opensourcedesign/fonts](https://github.com/opensourcedesign/fonts), since
the original Debian `fonts-freefont-ttf` package isn't reachable from every
build environment) than the 9/12/18/24pt sizes already in the file, and that
mirror's own yAdvance output diverged from the established anchors outside
of 12pt (which matched by coincidence). Those five sizes are patched to
values linearly interpolated between the established 9/12/18/24pt anchors
instead — see the header comment in `FreeSans.h`.

Full OFL 1.1 license text: https://openfontlicense.org
